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
#include <iostream>
#include <memory>
#include <vector>

#include <GL/gl.h>
#include <cxxopts.hpp>

#include "drmpp/input/seat.h"
#include "drmpp/logging/logging.h"
#include "drmpp/shared_libs/libdrm.h"
#include "drmpp/shared_libs/libegl.h"
#include "drmpp/shared_libs/libgbm.h"
#include "drmpp/utils/virtual_terminal.h"

#include "snake.h"

struct Configuration {
  std::string device = "/dev/dri/card0";
  size_t mode_index = 0;
};

static volatile bool gRunning = true;

class DrmSnakeApp final : public Logging,
                          public drmpp::utils::VirtualTerminal,
                          public drmpp::input::KeyboardObserver,
                          public drmpp::input::SeatObserver {
 public:
  explicit DrmSnakeApp(const Configuration& config) {
    device_ = config.device;
    mode_index_ = config.mode_index;

    init_drm();

    snake_initialize(&snake_ctx_);

    seat_ = std::make_unique<drmpp::input::Seat>(false, "");
    seat_->register_observer(this, this);
    seat_->run_once();
  }

  ~DrmSnakeApp() override { cleanup_drm(); }

  bool run() {
    while (gRunning) {
      seat_->run_once();
      snake_step(&snake_ctx_);
      render();
      std::this_thread::sleep_for(std::chrono::milliseconds(100));
    }
    cleanup_drm();

    return true;
  }

 private:
  void init_drm() {
    fd_ = open(device_.c_str(), O_RDWR);
    assert(fd_ != 0);

    const auto resources = drm->ModeGetResources(fd_);
    assert(resources != nullptr);

    const auto connector = find_connector(fd_, resources);
    assert(connector != nullptr);

    LOG_INFO("Using Mode: {} @ {}", connector->modes[mode_index_].name,
             connector->modes[mode_index_].vrefresh);

    drm_.connector_id = connector->connector_id;
    drm_.mode_info = connector->modes[mode_index_];

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

    egl_.display = egl->GetDisplay(gbm_.device);

    egl->Initialize(egl_.display, nullptr, nullptr);
    egl->BindAPI(EGL_OPENGL_API);

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

    egl->MakeCurrent(egl_.display, egl_.surface, egl_.surface, egl_.context);
  }

  void cleanup_drm() {
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
  }

  void render() {
    draw_snake();

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
    gbm_.previous_bo = gbm_.bo;
    drm_.previous_fb = drm_.fb;
  }

  void draw_snake() const {
    // Clear the screen
    glClear(GL_COLOR_BUFFER_BIT);

    // Set the color for the snake (green)
    glColor3f(0.0f, 1.0f, 0.0f);

    // Draw the snake
    for (const auto& [fst, snd] : snake_ctx_.snake) {
      glBegin(GL_QUADS);
      glVertex2f(static_cast<float>(fst) * 2.0f / SNAKE_GAME_WIDTH - 1.0f,
                 static_cast<float>(snd) * 2.0f / SNAKE_GAME_HEIGHT - 1.0f);
      glVertex2f(static_cast<float>(fst + 1) * 2.0f / SNAKE_GAME_WIDTH - 1.0f,
                 static_cast<float>(snd) * 2.0f / SNAKE_GAME_HEIGHT - 1.0f);
      glVertex2f(static_cast<float>(fst + 1) * 2.0f / SNAKE_GAME_WIDTH - 1.0f,
                 static_cast<float>(snd + 1) * 2.0f / SNAKE_GAME_HEIGHT - 1.0f);
      glVertex2f(static_cast<float>(fst) * 2.0f / SNAKE_GAME_WIDTH - 1.0f,
                 static_cast<float>(snd + 1) * 2.0f / SNAKE_GAME_HEIGHT - 1.0f);
      glEnd();
    }

    // Set the color for the food (red)
    glColor3f(1.0f, 0.0f, 0.0f);

    // Draw the food
    glBegin(GL_QUADS);
    glVertex2f(
        static_cast<float>(snake_ctx_.food.first) * 2.0f / SNAKE_GAME_WIDTH -
            1.0f,
        static_cast<float>(snake_ctx_.food.second) * 2.0f / SNAKE_GAME_HEIGHT -
            1.0f);
    glVertex2f(
        (static_cast<float>(snake_ctx_.food.first) + 1) * 2.0f /
                SNAKE_GAME_WIDTH -
            1.0f,
        static_cast<float>(snake_ctx_.food.second) * 2.0f / SNAKE_GAME_HEIGHT -
            1.0f);
    glVertex2f((static_cast<float>(snake_ctx_.food.first) + 1) * 2.0f /
                       SNAKE_GAME_WIDTH -
                   1.0f,
               (static_cast<float>(snake_ctx_.food.second) + 1) * 2.0f /
                       SNAKE_GAME_HEIGHT -
                   1.0f);
    glVertex2f(
        static_cast<float>(snake_ctx_.food.first) * 2.0f / SNAKE_GAME_WIDTH -
            1.0f,
        (static_cast<float>(snake_ctx_.food.second) + 1) * 2.0f /
                SNAKE_GAME_HEIGHT -
            1.0f);
    glEnd();
  }

  static drmModeConnector* find_connector(const int fd,
                                          const drmModeRes* resources) {
    std::vector connectors(resources->connectors,
                           resources->connectors + resources->count_connectors);
    for (const auto& it : connectors) {
      const auto connector = drm->ModeGetConnector(fd, it);
      if (connector->connection == DRM_MODE_CONNECTED) {
        return connector;
      }
      drm->ModeFreeConnector(connector);
    }
    return nullptr;
  }

  static drmModeEncoder* find_encoder(const int fd,
                                      const drmModeConnector* connector) {
    std::vector<uint32_t> encoders = {
        connector->encoders, connector->encoders + connector->count_encoders};
    for (const auto& it : encoders) {
      if (const auto encoder = drm->ModeGetEncoder(fd, it)) {
        return encoder;
      }
    }
    return nullptr;
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

  void notify_seat_capabilities(drmpp::input::Seat* seat,
                                uint32_t caps) override {
    LOG_INFO("Seat Capabilities: {}", caps);
    if (caps & SEAT_CAPABILITIES_KEYBOARD) {
      if (const auto keyboards = seat_->get_keyboards();
          keyboards.has_value()) {
        for (auto const& keyboard : *keyboards.value()) {
          keyboard->register_observer(this, this);
        }
      }
    }
  }

  void notify_keyboard_xkb_v1_key(
      drmpp::input::Keyboard* keyboard,
      uint32_t time,
      uint32_t xkb_scancode,
      bool keymap_key_repeats,
      const uint32_t state,
      int xdg_key_symbol_count,
      const xkb_keysym_t* xdg_key_symbols) override {
    if (state == LIBINPUT_KEY_STATE_PRESSED) {
      if (xdg_key_symbols[0] == XKB_KEY_Escape ||
          xdg_key_symbols[0] == XKB_KEY_q || xdg_key_symbols[0] == XKB_KEY_Q) {
        cleanup_drm();
        exit(EXIT_SUCCESS);
      }
      if (xdg_key_symbols[0] == XKB_KEY_r || xdg_key_symbols[0] == XKB_KEY_R) {
        // Restart the game as if the program was launched.
        snake_initialize(&snake_ctx_);
      } else if (xdg_key_symbols[0] == XKB_KEY_Up) {
        snake_redir(&snake_ctx_, SNAKE_DIR_DOWN);
      } else if (xdg_key_symbols[0] == XKB_KEY_Down) {
        snake_redir(&snake_ctx_, SNAKE_DIR_UP);
      } else if (xdg_key_symbols[0] == XKB_KEY_Left) {
        snake_redir(&snake_ctx_, SNAKE_DIR_LEFT);
      } else if (xdg_key_symbols[0] == XKB_KEY_Right) {
        snake_redir(&snake_ctx_, SNAKE_DIR_RIGHT);
      }
    }
  }

  std::unique_ptr<drmpp::input::Seat> seat_;
  SnakeContext snake_ctx_;

  std::string device_;
  size_t mode_index_;
  int fd_{};

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
};

int main(const int argc, char** argv) {
  std::signal(SIGINT, [](const int signal) {
    if (signal == SIGINT) {
      gRunning = false;
    }
  });

  Configuration config;
  cxxopts::Options options("drm-snake", "DRM Snake Game");
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
    std::cout << options.help() << std::endl;
    return EXIT_SUCCESS;
  }

  std::filesystem::path device_path(config.device);
  if (!std::filesystem::exists(config.device)) {
    LOG_CRITICAL("Device path does not exist: {}\nUse -d <path to dri device>",
                 config.device);
    return (EXIT_FAILURE);
  }

  DrmSnakeApp app(config);
  app.run();

  return EXIT_SUCCESS;
}
