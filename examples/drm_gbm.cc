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

struct Configuration {};

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
  explicit App(const Configuration& /* config */)
      : logging_(std::make_unique<Logging>()) {
    LibEgl::IsPresent();
    glClearColorFnptr_ = reinterpret_cast<GlClearColor>(
        LibEgl->get_proc_address("glClearColor"));
    assert(glClearColorFnptr_);
    glClearFnptr_ =
        reinterpret_cast<GlClear>(LibEgl->get_proc_address("glClear"));
    assert(glClearFnptr_);
  }

  ~App() = default;

  bool run() {
    device_ = open("/dev/dri/card0", O_RDWR);
    assert(device_ != 0);

    resources_ = LibDrm->mode_get_resources(device_);
    assert(resources_ != nullptr);

    connector_ = find_connector(resources_);
    assert(connector_ != nullptr);

    printf("Mode list:\n------------------\n");
    for (int i = 0; i < connector_->count_modes; i++) {
      printf("Mode %d: %s\n", i, connector_->modes[i].name);
    }

    connector_id_ = connector_->connector_id;
    mode_info_ = connector_->modes[0];

    encoder_ = find_encoder(connector_);
    assert(encoder_ != nullptr);

    crtc_ = LibDrm->mode_get_crtc(device_, encoder_->crtc_id);
    assert(crtc_ != nullptr);

    LibDrm->mode_free_encoder(encoder_);
    LibDrm->mode_free_connector(connector_);
    LibDrm->mode_free_resources(resources_);

    gbm_device_ = LibGbm->create_device(device_);
    assert(gbm_device_ != nullptr);

    gbm_surface_ = LibGbm->surface_create(
        gbm_device_, mode_info_.hdisplay, mode_info_.vdisplay,
        GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    assert(gbm_surface_ != nullptr);

    static EGLint attributes[] = {EGL_SURFACE_TYPE,
                                  EGL_WINDOW_BIT,
                                  EGL_RED_SIZE,
                                  8,
                                  EGL_GREEN_SIZE,
                                  8,
                                  EGL_BLUE_SIZE,
                                  8,
                                  EGL_ALPHA_SIZE,
                                  0,
                                  EGL_RENDERABLE_TYPE,
                                  EGL_OPENGL_ES2_BIT,
                                  EGL_NONE};

    static constexpr EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2,
                                                 EGL_NONE};

    display_ = LibEgl->get_display(
        reinterpret_cast<EGLNativeDisplayType>(gbm_device_));

    LibEgl->initialize(display_, nullptr, nullptr);
    LibEgl->bind_api(EGL_OPENGL_API);

    EGLint count = 0;
    LibEgl->get_configs(display_, nullptr, 0, &count);
    const auto configs =
        static_cast<EGLConfig*>(malloc(count * sizeof(EGLConfig)));
    EGLint num_config{};
    LibEgl->choose_config(display_, attributes, configs, count, &num_config);

    const int config_index = match_config_to_visual(
        display_, GBM_FORMAT_XRGB8888, configs, num_config);

    context_ = LibEgl->create_context(display_, configs[config_index],
                                      EGL_NO_CONTEXT, context_attribs);

    egl_surface_ = LibEgl->create_window_surface(
        display_, configs[config_index],
        reinterpret_cast<EGLNativeWindowType>(gbm_surface_), nullptr);
    free(configs);

    LibEgl->make_current(display_, egl_surface_, egl_surface_, context_);

    for (auto i = 0; i < 600; i++)
      draw(static_cast<float>(i) / 600.0f);

    LibDrm->mode_set_crtc(device_, crtc_->crtc_id, crtc_->buffer_id, crtc_->x,
                          crtc_->y, &connector_id_, 1, &crtc_->mode);
    LibDrm->mode_free_crtc(crtc_);

    if (previous_bo_) {
      LibDrm->mode_rm_fb(device_, previous_fb_);
      LibGbm->surface_release_buffer(gbm_surface_, previous_bo_);
    }

    LibEgl->destroy_surface(display_, egl_surface_);
    LibGbm->surface_destroy(gbm_surface_);
    LibEgl->destroy_context(display_, context_);
    LibEgl->terminate(display_);
    LibGbm->device_destroy(gbm_device_);

    close(device_);

    return false;
  }

 private:
  std::unique_ptr<Logging> logging_;

  GlClearColor glClearColorFnptr_ = nullptr;
  GlClear glClearFnptr_ = nullptr;

  int device_{};
  drmModeRes* resources_{};
  drmModeConnector* connector_{};
  uint32_t connector_id_{};
  drmModeEncoder* encoder_{};
  drmModeModeInfo mode_info_{};
  drmModeCrtc* crtc_{};
  gbm_device* gbm_device_{};
  EGLDisplay display_{};
  EGLContext context_{};
  gbm_surface* gbm_surface_{};
  EGLSurface egl_surface_{};

  gbm_bo* previous_bo_{};
  uint32_t previous_fb_{};

  gbm_bo* bo_{};
  uint32_t handle_{};
  uint32_t pitch_{};
  uint32_t fb_{};
  uint64_t modifier_{};

  drmModeConnector* find_connector(const drmModeRes* resources) const {
    for (auto i = 0; i < resources->count_connectors; i++) {
      drmModeConnector* connector =
          LibDrm->mode_get_connector(device_, resources->connectors[i]);
      if (connector->connection == DRM_MODE_CONNECTED) {
        return connector;
      }
      LibDrm->mode_free_connector(connector);
    }
    return nullptr;  // if no connector found
  }

  drmModeEncoder* find_encoder(const drmModeConnector* connector) const {
    return LibDrm->mode_get_encoder(device_, connector->encoder_id);
  }

  void swap_buffers() {
    LibEgl->swap_buffers(display_, egl_surface_);
    bo_ = LibGbm->surface_lock_front_buffer(gbm_surface_);
    handle_ = LibGbm->bo_get_handle(bo_).u32;
    pitch_ = LibGbm->bo_get_stride(bo_);
    LibDrm->mode_add_fb(device_, mode_info_.hdisplay, mode_info_.vdisplay, 24,
                        32, pitch_, handle_, &fb_);
    LibDrm->mode_set_crtc(device_, crtc_->crtc_id, fb_, 0, 0, &connector_id_, 1,
                          &mode_info_);
    if (previous_bo_) {
      LibDrm->mode_rm_fb(device_, previous_fb_);
      LibGbm->surface_release_buffer(gbm_surface_, previous_bo_);
    }
    previous_bo_ = bo_;
    previous_fb_ = fb_;
  }

  void draw(const float progress) {
    glClearColorFnptr_(1.0f - progress, progress, 0.0, 1.0);
    glClearFnptr_(GL_COLOR_BUFFER_BIT);
    swap_buffers();
  }

  static int match_config_to_visual(EGLDisplay egl_display,
                                    const EGLint& visual_id,
                                    const EGLConfig* configs,
                                    const int count) {
    EGLint id;
    for (auto i = 0; i < count; ++i) {
      assert(configs);
      assert(configs[i]);
      if (!LibEgl->get_config_attrib(egl_display, configs[i],
                                     EGL_NATIVE_VISUAL_ID, &id))
        continue;
      if (id == visual_id)
        return i;
    }
    return -1;
  }
};

int main(const int argc, char** argv) {
  std::signal(SIGINT, handle_signal);

  cxxopts::Options options("drm-simple", "Simple DRM example");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()("help", "Print help");

  if (options.parse(argc, argv).count("help")) {
    spdlog::info("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  App app({});

  (void)app.run();

  return EXIT_SUCCESS;
}
