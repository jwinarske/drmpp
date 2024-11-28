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

#include <utils/virtual_terminal.h>
#include <cxxopts.hpp>

#include "drmpp.h"
#include "input/default_left_ptr.h"
#include "shared_libs/libdrm.h"
#include "shared_libs/libgbm.h"
#include "utils/utils.h"

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
      : logging_(std::make_unique<Logging>()),
        width_(0),
        height_(0),
        pointer_x_(0.0),
        pointer_y_(0.0) {
    seat_ = std::make_unique<drmpp::input::Seat>(false, "");
    seat_->register_observer(this, this);
  }

  ~App() override { seat_.reset(); }

  [[nodiscard]] bool run() {
    using namespace drmpp;

    if (!ensureInitialized()) {
      return false;
    }

    seat_->run_once();

    Composition composition;
    composition.addLayer({
        fb_,
        {0, 0, width_, height_},
        {0, 0, width_, height_},
    });

    composition.addPointerLayer({
        .buffer = cursor_->buffer,
        .hot_x = cursor_->hot_x,
        .hot_y = cursor_->hot_y,
        .x = static_cast<uint32_t>(std::round(pointer_x_)),
        .y = static_cast<uint32_t>(std::round(pointer_y_)),
    });

    output_->present(composition);
    return true;
  }

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
        std::scoped_lock lock(cmd_mutex_);
        exit(EXIT_SUCCESS);
      }
      if (xdg_key_symbols[0] == XKB_KEY_d) {
        std::scoped_lock lock(cmd_mutex_);
      } else if (xdg_key_symbols[0] == XKB_KEY_b) {
        std::scoped_lock lock(cmd_mutex_);
      }
    }
    LOG_INFO(
        "Key: time: {}, xkb_scancode: 0x{:X}, key_repeats: {}, state: {}, "
        "xdg_keysym_count: {}, syms_out[0]: 0x{:X}",
        time, xkb_scancode, keymap_key_repeats,
        state == LIBINPUT_KEY_STATE_PRESSED ? "press" : "release",
        xdg_key_symbol_count, xdg_key_symbols[0]);
  }

  void notify_pointer_motion(drmpp::input::Pointer *pointer,
                             uint32_t time,
                             double sx,
                             double sy) override {
    LOG_TRACE("dx: {}, dy: {}", sx, sy);
    pointer_x_ = std::clamp(pointer_x_ + sx, 0.0, static_cast<double>(width_));
    pointer_y_ = std::clamp(pointer_y_ + sy, 0.0, static_cast<double>(height_));
  }

  void notify_pointer_motion_absolute(drmpp::input::Pointer *pointer,
                                      uint32_t time,
                                      double ndc_x,
                                      double ndc_y) override {
    pointer_x_ = static_cast<double>(width_) * ndc_x;
    pointer_y_ = static_cast<double>(height_) * ndc_y;
    LOG_DEBUG("x: {}, y: {}", pointer_x_, pointer_y_);
  }

  void notify_pointer_button(drmpp::input::Pointer *pointer,
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
  struct LoadedCursor {
    std::shared_ptr<drmpp::Buffer> buffer;
    uint32_t hot_x, hot_y;
  };

  std::unique_ptr<Logging> logging_;
  std::unique_ptr<drmpp::input::Seat> seat_;
  std::mutex cmd_mutex_{};

  std::unique_ptr<drmpp::Device> device_;
  std::unique_ptr<drmpp::Output> output_;
  std::shared_ptr<drmpp::Buffer> fb_;
  std::optional<LoadedCursor> cursor_;

  std::uint32_t width_, height_;
  double pointer_x_, pointer_y_;

  static std::unique_ptr<drmpp::Device> openDevice() {
    for (const auto& node : drmpp::utils::get_enabled_drm_nodes()) {
      return drmpp::Device::open(node);
    }

    return nullptr;
  }

  static std::optional<LoadedCursor> loadCursor(drmpp::Device& device) {
    // TODO: Move this somewhere so it's usable by everyone.
    const auto cursor_bytes = CursorLeftPtr.decompress();

    const auto cursor = drmpp::XCursor::load_images(cursor_bytes, 24);
    if (!cursor) {
      LOG_ERROR("Could not load default cursor file.");
      return {};
    }

    if (cursor->images.empty()) {
      LOG_ERROR("Default cursor has no valid cursor images.");
      return {};
    }

    const auto* image = cursor->images.front().get();

    uint32_t width = image->width;
    uint32_t height = image->height;

    // Drivers are sometimes picky with their cursor size.
    // The KMS driver present in QEMU accepts any cursor size,
    // but will only actually show the cursor on screen when
    // it matches the desired size (64x64).
    if (const auto desired = device.desiredCursorSize()) {
      width = desired->first;
      height = desired->second;
    }

    auto fb = device.createDumbBuffer(width, height, 32, DRM_FORMAT_ARGB8888,
                                      device.supportsModifiers()
                                          ? DRM_FORMAT_MOD_LINEAR
                                          : DRM_FORMAT_MOD_INVALID);
    if (!fb) {
      return {};
    }

    uint32_t offset;
    uint32_t stride;
    std::size_t size;

    void* mapped = fb->map(offset, stride, size);

    // If the layout of the created framebuffer perfectly matches the
    // layout of the X11 cursor image, we can just copy over the whole thing.
    if (stride == image->width * 4 && size - offset == stride * image->height) {
      std::memcpy(static_cast<uint8_t*>(mapped) + offset, image->pixels.data(),
                  image->pixels.size() * 4);
    } else {
      // otherwise, we copy line-by-line.
      for (uint32_t y = 0; y < image->height; y++) {
        const auto dst_offset = offset + y * stride;
        const auto src_stride = image->width * 4;
        const auto src_offset = y * src_stride;

        // Just some paranoid check that we don't write/read out of bounds.
        assert(dst_offset + src_stride <= size - offset);
        assert(src_offset + src_stride <= image->pixels.size() * 4);

        std::memcpy(
            static_cast<uint8_t*>(mapped) + dst_offset,
            reinterpret_cast<const uint8_t*>(image->pixels.data()) + src_offset,
            src_stride);
      }
    }

    fb->unmap(mapped);

    return LoadedCursor{
        .buffer = std::move(fb),
        .hot_x = image->xhot,
        .hot_y = image->yhot,
    };
  }

  bool ensureInitialized() {
    if (device_) {
      // Either all should be initialized, or none.
      assert(output_ && fb_ && cursor_);
      return true;
    }

    assert(!device_ && !output_ && !fb_ && !cursor_);

    device_ = openDevice();
    if (!device_) {
      return false;
    }

    output_ = device_->openFirstConnectedOutput();
    if (!output_) {
      device_.reset();
      return false;
    }

    LOG_INFO("Using connector {}, CRTC {}", output_->connector_id(),
             output_->crtc_id());

    const auto mode = output_->mode();
    LOG_INFO("Mode: {}x{}@{}Hz", mode.hdisplay, mode.vdisplay,
             output_->refreshRate());

    width_ = mode.hdisplay;
    height_ = mode.vdisplay;

    fb_ = device_->createDumbBuffer(width_, height_, 32, DRM_FORMAT_XRGB8888,
                                    device_->supportsModifiers()
                                        ? DRM_FORMAT_MOD_LINEAR
                                        : DRM_FORMAT_MOD_INVALID);
    if (!fb_) {
      output_.reset();
      device_.reset();
      return false;
    }

    fb_->fill(0xFFFFFFFF);

    auto loaded = loadCursor(*device_);
    if (!loaded) {
      output_.reset();
      device_.reset();
      return false;
    }

    cursor_ = std::move(loaded);
    return true;
  }
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

  App app({});

  while (gRunning && app.run()) {
  }

  return EXIT_SUCCESS;
}
