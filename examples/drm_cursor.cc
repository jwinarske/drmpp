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
#include <fstream>
#include <iostream>

#include <cxxopts.hpp>

#include "drmpp.h"

struct Configuration {};

static volatile bool gRunning = true;

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

class App final : public drmpp::input::KeyboardObserver,
                  public drmpp::input::PointerObserver,
                  public drmpp::input::SeatObserver {
 public:
  explicit App(const Configuration& /*config */)
      : logging_(std::make_unique<Logging>()) {
    seat_ = std::make_unique<drmpp::input::Seat>(false, "");
    seat_->register_observer(this, this);
  }

  ~App() override { seat_.reset(); }

  [[nodiscard]] bool run() const { return seat_->run_once(); }

  void notify_seat_capabilities(drmpp::input::Seat* seat,
                                uint32_t caps) override {
    LOG_INFO("Seat Capabilities: {}", caps);
    if (caps & SEAT_CAPABILITIES_POINTER) {
      if (const auto pointer = seat_->get_pointer(); pointer.has_value()) {
        pointer.value()->register_observer(this, this);
      }
    }
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
        std::scoped_lock<std::mutex> lock(cmd_mutex_);
        exit(EXIT_SUCCESS);
      }
      if (xdg_key_symbols[0] == XKB_KEY_d) {
        std::scoped_lock<std::mutex> lock(cmd_mutex_);
      } else if (xdg_key_symbols[0] == XKB_KEY_b) {
        std::scoped_lock<std::mutex> lock(cmd_mutex_);
      }
    }
    LOG_INFO(
        "Key: time: {}, xkb_scancode: 0x{:X}, key_repeats: {}, state: {}, "
        "xdg_keysym_count: {}, syms_out[0]: 0x{:X}",
        time, xkb_scancode, keymap_key_repeats,
        state == LIBINPUT_KEY_STATE_PRESSED ? "press" : "release",
        xdg_key_symbol_count, xdg_key_symbols[0]);
  }

  void notify_pointer_motion(drmpp::input::Pointer* pointer,
                             uint32_t time,
                             double sx,
                             double sy) override {
    LOG_TRACE("x: {}, y: {}", sx, sy);
  }

  void notify_pointer_button(drmpp::input::Pointer* pointer,
                             uint32_t serial,
                             uint32_t time,
                             uint32_t button,
                             uint32_t state) override {
    LOG_INFO("button: {}, state: {}", button, state);
  }

  void notify_pointer_axis(drmpp::input::Pointer* pointer,
                           uint32_t time,
                           uint32_t axis,
                           double value) override {
    LOG_INFO("axis: {}", axis);
  }

  void notify_pointer_frame(drmpp::input::Pointer* pointer) override {
    LOG_INFO("frame");
  }

  void notify_pointer_axis_source(drmpp::input::Pointer* pointer,
                                  uint32_t axis_source) override {
    LOG_INFO("axis_source: {}", axis_source);
  }

  void notify_pointer_axis_stop(drmpp::input::Pointer* pointer,
                                uint32_t time,
                                uint32_t axis) override {
    LOG_INFO("axis_stop: {}", axis);
  }

  void notify_pointer_axis_discrete(drmpp::input::Pointer* pointer,
                                    uint32_t axis,
                                    int32_t discrete) override {
    LOG_INFO("axis_discrete: axis: {}, discrete: {}", axis, discrete);
  }

 private:
  std::unique_ptr<Logging> logging_;
  std::unique_ptr<drmpp::input::Seat> seat_;
  std::mutex cmd_mutex_{};
};

int main(const int argc, char** argv) {
  std::signal(SIGINT, handle_signal);

  cxxopts::Options options("drm-cursor", "Render Cursor");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()("help", "Print help");

  if (options.parse(argc, argv).count("help")) {
    spdlog::info("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  const App app({});

  while (gRunning && app.run()) {
  }

  return EXIT_SUCCESS;
}
