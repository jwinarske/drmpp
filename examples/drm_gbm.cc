/*
 * Copyright (c) 2024 The drmpp Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <csignal>
#include <filesystem>
#include <iostream>

#include <GL/gl.h>

#include "shared_libs/libdrm.h"
#include "shared_libs/libegl.h"
#include "shared_libs/libgbm.h"

#include <cxxopts.hpp>

#include "drmpp.h"

struct Configuration {
  std::string device;
};

static volatile bool gRunning = true;

typedef void (*GlClearColor)(GLclampf red,
                             GLclampf green,
                             GLclampf blue,
                             GLclampf alpha);

typedef void (*GlClear)(GLbitfield mask);

/**
 * @brief Signal handler function to handle signals.
 *
 * This function is a signal handler for handling signals. It sets the value of
 * keep_running to false, which will stop the program from running. The function
 * does not take any input parameters.
 *
 * @param signal The signal number. This parameter is not used by the function.
 *
 * @return void
 */
void handle_signal(const int signal) {
  if (signal == SIGINT) {
    gRunning = false;
  }
}

class App final {
public:
  explicit App(const Configuration &config)
    : logging_(std::make_unique<Logging>()) {
    glClearColorFnptr_ =
        reinterpret_cast<GlClearColor>(egl->GetProcAddress("glClearColor"));
    assert(glClearColorFnptr_);
    glClearFnptr_ = reinterpret_cast<GlClear>(egl->GetProcAddress("glClear"));
    assert(glClearFnptr_);
    device_ = config.device;
  }

  ~App() = default;

  bool run() {
    fd_ = open(device_.c_str(), O_RDWR);
    assert(fd_ != 0);

    const auto resources = drm->ModeGetResources(fd_);
    assert(resources != nullptr);

    const auto connector = find_connector(fd_, resources);
    assert(connector != nullptr);

    printf("Mode list:\n------------------\n");
    for (int i = 0; i < connector->count_modes; i++) {
      printf("Mode %d: %s\n", i, connector->modes[i].name);
    }

    drm_.connector_id = connector->connector_id;
    drm_.mode_info = connector->modes[0];

    const auto encoder = find_encoder(fd_, connector);
    assert(encoder != nullptr);

    drm_.crtc = drm->ModeGetCrtc(fd_, encoder->crtc_id);
    assert(drm_.crtc != nullptr);

    drm->ModeFreeEncoder(encoder);
    drm->ModeFreeConnector(connector);
    drm->ModeFreeResources(resources);

    gbm_.device = gbm->create_device(fd_);
    assert(gbm_.device != nullptr);

    gbm_.surface = gbm->surface_create(
      gbm_.device, drm_.mode_info.hdisplay, drm_.mode_info.vdisplay,
      GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    assert(gbm_.surface != nullptr);

    static EGLint attributes[] = {
        // clang-format off
      EGL_SURFACE_TYPE, EGL_WINDOW_BIT,
      EGL_RED_SIZE, 8,
      EGL_GREEN_SIZE, 8,
      EGL_BLUE_SIZE, 8,
      EGL_ALPHA_SIZE, 0,
      EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE
      // clang-format on
    };

    static constexpr EGLint context_attribs[] = {
        // clang-format off
      EGL_CONTEXT_CLIENT_VERSION, 2,
      EGL_NONE
      // clang-format on
    };

    const auto display = egl->GetDisplay(gbm_.device);

    egl->Initialize(display, nullptr, nullptr);
    egl->BindAPI(EGL_OPENGL_API);

    EGLint count = 0;
    egl->GetConfigs(display, nullptr, 0, &count);
    const auto configs =
        static_cast<EGLConfig *>(malloc(count * sizeof(EGLConfig)));
    EGLint num_config{};
    egl->ChooseConfig(display, attributes, configs, count, &num_config);

    const int config_index = match_config_to_visual(
      display, GBM_FORMAT_XRGB8888, configs, num_config);

    egl_.context = egl->CreateContext(display, configs[config_index],
                                      EGL_NO_CONTEXT, context_attribs);

    egl_.surface = egl->CreateWindowSurface(
      display, configs[config_index],
      reinterpret_cast<EGLNativeWindowType>(gbm_.surface), nullptr);
    free(configs);

    egl->MakeCurrent(display, egl_.surface, egl_.surface, egl_.context);

    for (auto i = 0; i < 600; i++)
      draw(static_cast<float>(i) / 600.0f);

    drm->ModeSetCrtc(fd_, drm_.crtc->crtc_id, drm_.crtc->buffer_id,
                     drm_.crtc->x, drm_.crtc->y, &drm_.connector_id, 1,
                     &drm_.crtc->mode);
    drm->ModeFreeCrtc(drm_.crtc);

    if (gbm_.previous_bo) {
      drm->ModeRmFB(fd_, drm_.previous_fb);
      gbm->surface_release_buffer(gbm_.surface, gbm_.previous_bo);
    }

    egl->DestroySurface(display, egl_.surface);
    gbm->surface_destroy(gbm_.surface);
    egl->DestroyContext(display, egl_.context);
    egl->Terminate(display);
    gbm->device_destroy(gbm_.device);

    close(fd_);

    return false;
  }

private:
  std::unique_ptr<Logging> logging_;

  GlClearColor glClearColorFnptr_ = nullptr;
  GlClear glClearFnptr_ = nullptr;

  std::string device_;
  int fd_{};

  struct {
    drmModeModeInfo mode_info;
    drmModeCrtc *crtc;
    uint32_t connector_id;
    uint32_t fb;
    uint32_t previous_fb;
  } drm_{};

  struct {
    EGLDisplay display;
    EGLContext context;
    EGLSurface surface;
  } egl_{};

  struct {
    gbm_device *device;
    gbm_surface *surface;
    gbm_bo *bo;
    gbm_bo *previous_bo;
  } gbm_{};

  static drmModeConnector *find_connector(const int fd,
                                          const drmModeRes *resources) {
    for (auto i = 0; i < resources->count_connectors; i++) {
      drmModeConnector *connector =
          drm->ModeGetConnector(fd, resources->connectors[i]);
      if (connector->connection == DRM_MODE_CONNECTED) {
        return connector;
      }
      drm->ModeFreeConnector(connector);
    }
    return nullptr; // if no connector found
  }

  static drmModeEncoder *find_encoder(const int fd,
                                      const drmModeConnector *connector) {
    return drm->ModeGetEncoder(fd, connector->encoder_id);
  }

  void swap_buffers() {
    egl->SwapBuffers(egl_.display, egl_.surface);
    gbm_.bo = gbm->surface_lock_front_buffer(gbm_.surface);
    const auto handle = gbm->bo_get_handle(gbm_.bo).u32;
    const auto pitch = gbm->bo_get_stride(gbm_.bo);
    drm->ModeAddFB(fd_, drm_.mode_info.hdisplay, drm_.mode_info.vdisplay, 24,
                   32, pitch, handle, &drm_.fb);
    drm->ModeSetCrtc(fd_, drm_.crtc->crtc_id, drm_.fb, 0, 0, &drm_.connector_id,
                     1, &drm_.mode_info);
    if (gbm_.previous_bo) {
      drm->ModeRmFB(fd_, drm_.previous_fb);
      gbm->surface_release_buffer(gbm_.surface, gbm_.previous_bo);
    }
    drm_.previous_fb = drm_.fb;
    gbm_.previous_bo = gbm_.bo;
  }

  void draw(const float progress) {
    glClearColorFnptr_(1.0f - progress, progress, 0.0, 1.0);
    glClearFnptr_(GL_COLOR_BUFFER_BIT);
    swap_buffers();
  }

  static int match_config_to_visual(EGLDisplay egl_display,
                                    const EGLint &visual_id,
                                    const EGLConfig *configs,
                                    const int count) {
    EGLint id;
    for (auto i = 0; i < count; ++i) {
      assert(configs);
      assert(configs[i]);
      if (!egl->GetConfigAttrib(egl_display, configs[i], EGL_NATIVE_VISUAL_ID,
                                &id))
        continue;
      if (id == visual_id)
        return i;
    }
    return -1;
  }
};

int main(const int argc, char **argv) {
  std::signal(SIGINT, handle_signal);

  Configuration config;
  cxxopts::Options options("drm-gbm", "DRM GBM example");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()
      // clang-format off
      ("help", "Print help")
      ("d,device", "Path to device", cxxopts::value<std::string>(config.device));
  // clang-format on

  std::filesystem::path device_path(config.device);
  if (!std::filesystem::exists(config.device)) {
    LOG_ERROR("Device path does not exist: {}\nUse -d <path to dri device>",
              config.device);
    exit(EXIT_SUCCESS);
  }

  if (options.parse(argc, argv).count("help")) {
    LOG_INFO("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  App app(config);

  (void) app.run();

  return EXIT_SUCCESS;
}
