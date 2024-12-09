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

#include <fcntl.h>
#include <src/core/lv_global.h>
#include <unistd.h>

#include <csignal>
#include <cxxopts.hpp>
#include <filesystem>

#include "drmpp/logging/logging.h"
#include "drmpp/shared_libs/libdrm.h"
#include "drmpp/shared_libs/libegl.h"
#include "drmpp/shared_libs/libgbm.h"
#include "drmpp/utils/virtual_terminal.h"

extern "C" {
#include "demos/lv_demos.h"
}

struct Configuration {
  std::string device = "/dev/dri/card0";
  size_t mode_index = 0;
};

static volatile bool gRunning = true;

class App final : public Logging, public drmpp::utils::VirtualTerminal {
 public:
  struct lv_egl_window_t {
    EGLSurface surface;
    int32_t hor_res;
    int32_t ver_res;
    lv_ll_t textures;
    lv_point_t mouse_last_point;
    lv_indev_state_t mouse_last_state;
    uint8_t use_indev : 1;
    uint8_t closing : 1;
  };

  struct lv_egl_texture_t {
    lv_egl_window_t* window;
    unsigned int texture_id;
    lv_area_t area;
    lv_opa_t opa;
    lv_indev_t* indev;
    lv_point_t indev_last_point;
    lv_indev_state_t indev_last_state;
  };

  explicit App(const Configuration& config) : egl_window_(nullptr) {
    device_ = config.device;
    mode_index_ = config.mode_index;
    LOG_INFO("Device: {}", device_);
  }

  ~App() override { lv_deinit(); }

  bool run() {
    fd_ = open(device_.c_str(), O_RDWR);
    if (fd_ < 0) {
      LOG_ERROR("Failed to open device: {}", device_);
      return false;
    }

    const auto resources = drm->ModeGetResources(fd_);
    if (resources == nullptr) {
      LOG_ERROR("Failed to get DRM resources");
      close(fd_);
      return false;
    }

    const auto connector = find_connector(fd_, resources);
    if (connector == nullptr) {
      LOG_ERROR("Failed to find a suitable connector");
      drm->ModeFreeResources(resources);
      close(fd_);
      return false;
    }

    LOG_INFO("Mode list");
    LOG_INFO("------------------");
    for (int i = 0; i < connector->count_modes; i++) {
      LOG_INFO("Mode {}: {} @ {}", i, connector->modes[i].name,
               connector->modes[i].vrefresh);
    }
    LOG_INFO("Using Mode: {} @ {}", connector->modes[mode_index_].name,
             connector->modes[mode_index_].vrefresh);

    drm_.connector_id = connector->connector_id;
    drm_.mode_info = connector->modes[mode_index_];

    const auto encoder = find_encoder(fd_, connector);
    if (encoder == nullptr) {
      LOG_ERROR("Failed to find a suitable encoder");
      drm->ModeFreeConnector(connector);
      drm->ModeFreeResources(resources);
      close(fd_);
      return false;
    }

    drm_.crtc = drm->ModeGetCrtc(fd_, encoder->crtc_id);
    if (drm_.crtc == nullptr) {
      LOG_ERROR("Failed to get CRTC");
      drm->ModeFreeEncoder(encoder);
      drm->ModeFreeConnector(connector);
      drm->ModeFreeResources(resources);
      close(fd_);
      return false;
    }

    drm->ModeFreeEncoder(encoder);
    drm->ModeFreeConnector(connector);
    drm->ModeFreeResources(resources);

    gbm_.device = gbm->create_device(fd_);
    if (gbm_.device == nullptr) {
      LOG_ERROR("Failed to create GBM device");
      close(fd_);
      return false;
    }

    gbm_.surface = gbm->surface_create(
        gbm_.device, drm_.mode_info.hdisplay, drm_.mode_info.vdisplay,
        GBM_FORMAT_XRGB8888, GBM_BO_USE_SCANOUT | GBM_BO_USE_RENDERING);
    if (gbm_.surface == nullptr) {
      LOG_ERROR("Failed to create GBM surface");
      gbm->device_destroy(gbm_.device);
      close(fd_);
      return false;
    }

    egl_.display =
        egl->GetDisplay(reinterpret_cast<EGLNativeDisplayType>(gbm_.device));
    if (egl_.display == EGL_NO_DISPLAY) {
      LOG_ERROR("Failed to get EGL display");
      gbm->surface_destroy(gbm_.surface);
      gbm->device_destroy(gbm_.device);
      close(fd_);
      return false;
    }

    if (!egl->Initialize(egl_.display, nullptr, nullptr)) {
      LOG_ERROR("Failed to initialize EGL");
      egl->Terminate(egl_.display);
      gbm->surface_destroy(gbm_.surface);
      gbm->device_destroy(gbm_.device);
      close(fd_);
      return false;
    }

    eglSwapInterval(egl_.display, 1);

    lv_init();
    lv_ll_init(&egl_window_ll_, sizeof(lv_egl_window_t));

    egl_window_ = lv_egl_window_create(drm_.mode_info.hdisplay,
                                       drm_.mode_info.vdisplay, true);

    lv_display_t* texture = lv_opengles_texture_create(drm_.mode_info.hdisplay,
                                                       drm_.mode_info.vdisplay);
    lv_display_set_default(texture);

    // add the texture to the window
    const unsigned int texture_id = lv_opengles_texture_get_texture_id(texture);
    const lv_egl_texture_t* window_texture = lv_drm_window_add_texture(
        egl_window_, texture_id, drm_.mode_info.hdisplay,
        drm_.mode_info.vdisplay);

#if 0  // TODO mouse cursor
    // get the mouse index of the window texture
    lv_indev_t* mouse = lv_texture_get_mouse_indev(window_texture);
    LV_IMAGE_DECLARE(mouse_cursor_icon);
    lv_obj_t* cursor_obj = lv_image_create(lv_screen_active());
    lv_image_set_src(cursor_obj, &mouse_cursor_icon);
    lv_indev_set_cursor(mouse, cursor_obj);
#endif

    // create objects on the screen
    lv_demo_widgets();

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

    egl->DestroySurface(egl_.display, egl_.surface);
    gbm->surface_destroy(gbm_.surface);
    egl->DestroyContext(egl_.display, egl_.context);
    egl->Terminate(egl_.display);
    gbm->device_destroy(gbm_.device);

    close(fd_);

    return false;
  }

  lv_egl_window_t* lv_egl_window_create(const int32_t hor_res,
                                        const int32_t ver_res,
                                        const bool use_mouse_indev) {
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

    auto* window =
        static_cast<lv_egl_window_t*>(lv_ll_ins_tail(&egl_window_ll_));
    LV_ASSERT_MALLOC(window);
    lv_memzero(window, sizeof(lv_egl_window_t));

    // Create window with graphics context
    EGLint count = 0;
    egl->GetConfigs(egl_.display, nullptr, 0, &count);
    const auto configs =
        static_cast<EGLConfig*>(malloc(count * sizeof(EGLConfig)));
    EGLint num_config{};
    egl->ChooseConfig(egl_.display, attributes, configs, count, &num_config);

    const int config_index = match_config_to_visual(
        egl_.display, GBM_FORMAT_XRGB8888, configs, num_config);

    egl_.context = egl->CreateContext(egl_.display, configs[config_index],
                                      EGL_NO_CONTEXT, context_attribs);

    egl_.surface = egl->CreateWindowSurface(
        egl_.display, configs[config_index],
        reinterpret_cast<EGLNativeWindowType>(gbm_.surface), nullptr);
    free(configs);

    window->surface = egl_.surface;
    window->hor_res = hor_res;
    window->ver_res = ver_res;
    lv_ll_init(&window->textures, sizeof(lv_egl_texture_t));
    window->use_indev = use_mouse_indev;

    egl->MakeCurrent(egl_.display, egl_.surface, egl_.surface, egl_.context);
    lv_opengles_init();

    return window;
  }

  static lv_egl_texture_t* lv_drm_window_add_texture(
      lv_egl_window_t* window,
      const unsigned int texture_id,
      const int32_t w,
      const int32_t h) {
    auto* texture =
        static_cast<lv_egl_texture_t*>(lv_ll_ins_tail(&window->textures));
    LV_ASSERT_MALLOC(texture);
    lv_memzero(texture, sizeof(*texture));
    texture->window = window;
    texture->texture_id = texture_id;
    lv_area_set(&texture->area, 0, 0, w - 1, h - 1);
    texture->opa = LV_OPA_COVER;

    if (window->use_indev) {
      lv_display_t* texture_disp =
          lv_opengles_texture_get_from_texture_id(texture_id);
      if (texture_disp != nullptr) {
        lv_indev_t* indev = lv_indev_create();
        if (indev == nullptr) {
          lv_ll_remove(&window->textures, texture);
          lv_free(texture);
          return nullptr;
        }
        texture->indev = indev;
        lv_indev_set_type(indev, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(indev, indev_read_cb);
        lv_indev_set_driver_data(indev, texture);
        lv_indev_set_mode(indev, LV_INDEV_MODE_EVENT);
        lv_indev_set_display(indev, texture_disp);
      }
    }

    return texture;
  }

  static void indev_read_cb(lv_indev_t* indev, lv_indev_data_t* data) {
    const auto* texture =
        static_cast<lv_egl_texture_t*>(lv_indev_get_driver_data(indev));
    data->point = texture->indev_last_point;
    data->state = texture->indev_last_state;
  }

  static void lv_texture_remove(lv_egl_texture_t* texture) {
    if (texture->indev != nullptr) {
      lv_indev_delete(texture->indev);
    }
    lv_ll_remove(&texture->window->textures, texture);
    lv_free(texture);
  }

  static lv_indev_t* lv_texture_get_mouse_indev(
      const lv_egl_texture_t* texture) {
    return texture->indev;
  }

 private:
  std::string device_;
  size_t mode_index_;
  int fd_{};

  lv_egl_window_t* egl_window_;
  lv_ll_t egl_window_ll_{};

  struct {
    drmModeModeInfo mode_info;
    drmModeCrtc* crtc;
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
    gbm_device* device;
    gbm_surface* surface;
    gbm_bo* bo;
    gbm_bo* previous_bo;
  } gbm_{};

  static drmModeConnector* find_connector(const int fd,
                                          const drmModeRes* resources) {
    for (auto i = 0; i < resources->count_connectors; i++) {
      drmModeConnector* connector =
          drm->ModeGetConnector(fd, resources->connectors[i]);
      if (connector->connection == DRM_MODE_CONNECTED) {
        return connector;
      }
      drm->ModeFreeConnector(connector);
    }
    return nullptr;  // if no connector found
  }

  static drmModeEncoder* find_encoder(const int fd,
                                      const drmModeConnector* connector) {
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
    const auto start_time =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count();

    void* window = nullptr;

    // render each window
    LV_LL_READ(&egl_window_ll_, window) {
      egl->MakeCurrent(egl_.display, egl_.surface, egl_.surface, egl_.context);
      lv_opengles_viewport(0, 0, static_cast<lv_egl_window_t*>(window)->hor_res,
                           static_cast<lv_egl_window_t*>(window)->ver_res);
      lv_opengles_render_clear();

      // render each texture in the window
      void* texture;
      LV_LL_READ(&egl_window_->textures, texture) {
        // if the added texture is an LVGL opengles texture display,
        // refresh it before rendering it
        lv_display_t* texture_disp = lv_opengles_texture_get_from_texture_id(
            static_cast<lv_egl_texture_t*>(texture)->texture_id);
        if (texture_disp != nullptr) {
          lv_refr_now(texture_disp);
        }
        lv_opengles_render_texture(
            static_cast<lv_egl_texture_t*>(texture)->texture_id,
            &static_cast<lv_egl_texture_t*>(texture)->area,
            static_cast<lv_egl_texture_t*>(texture)->opa,
            static_cast<lv_egl_window_t*>(window)->hor_res,
            static_cast<lv_egl_window_t*>(window)->ver_res);
      }
      swap_buffers();
    }

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(
            std::chrono::steady_clock::now().time_since_epoch())
            .count() -
        start_time;
    if (const auto sleep_time = 16 - elapsed; sleep_time > 0) {
      std::this_thread::sleep_for(std::chrono::milliseconds(sleep_time));
    }
  }

  static int match_config_to_visual(EGLDisplay egl_display,
                                    const EGLint& visual_id,
                                    const EGLConfig* configs,
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

int main(const int argc, char** argv) {
  std::signal(SIGINT, [](const int signal) {
    if (signal == SIGINT) {
      gRunning = false;
    }
  });

  Configuration config;
  cxxopts::Options options("drm-gbm", "DRM GBM example");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()
      // clang-format off
      ("help", "Print help")
      ("d,device", "Path to device", cxxopts::value<std::string>(config.device))
      ("m,mode", "Mode index", cxxopts::value<size_t>(config.mode_index));
  // clang-format on

  if (options.parse(argc, argv).count("help")) {
    LOG_INFO("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  std::filesystem::path device_path(config.device);
  if (!std::filesystem::exists(config.device)) {
    LOG_ERROR("Device path does not exist: {}\nUse -d <path to dri device>",
              config.device);
    exit(EXIT_SUCCESS);
  }

  App app(config);

  (void)app.run();

  return EXIT_SUCCESS;
}
