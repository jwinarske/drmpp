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

#include <libinput.h>
#include <shared_libs/libdrm.h>

extern "C" {
#include <libdisplay-info/info.h>
}

#include <info/info.h>
#include <input/keyboard.h>
#include <utils.h>
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
                  public drmpp::input::SeatObserver {
 public:
  explicit App(const Configuration& /*config */)
      : logging_(std::make_unique<Logging>()) {
    seat_ = std::make_unique<drmpp::input::Seat>(false, "");
    seat_->register_observer(this, this);
  }

  ~App() override { seat_.reset(); }

  [[nodiscard]] bool run() const { return seat_->run_once(); }

  static void print_di_info(const di_info* info) {
    auto str = di_info_get_make(info);
    LOG_INFO("make: [{}]", str ? str : "");
    free(str);

    str = di_info_get_model(info);
    LOG_INFO("model: [{}]", str ? str : "");
    free(str);

    str = di_info_get_serial(info);
    LOG_INFO("serial: [{}]", str ? str : "");
    free(str);

    const auto hdr_static = di_info_get_hdr_static_metadata(info);
    assert(hdr_static);
    LOG_INFO("HDR static metadata");
    LOG_INFO("\tluminance {:f}-{:f}, maxFALL {:f}",
             hdr_static->desired_content_min_luminance,
             hdr_static->desired_content_max_luminance,
             hdr_static->desired_content_max_frame_avg_luminance);
    LOG_INFO("\tmetadata type1: {}", hdr_static->type1 ? "yes" : "no");
    LOG_INFO("\tEOTF tSDR: {}, tHDR: {}, PQ: {}, HLG: {}",
             hdr_static->traditional_sdr ? "yes" : "no",
             hdr_static->traditional_hdr ? "yes" : "no",
             hdr_static->pq ? "yes" : "no", hdr_static->hlg ? "yes" : "no");

    const auto primaries = di_info_get_default_color_primaries(info);
    assert(primaries);
    LOG_INFO("default color primaries");
    LOG_INFO("\t{}: {:.3f}, {:.3f}", "    red", primaries->primary[0].x,
             primaries->primary[0].y);
    LOG_INFO("\t{}: {:.3f}, {:.3f}", "  green", primaries->primary[1].x,
             primaries->primary[1].y);
    LOG_INFO("\t{}: {:.3f}, {:.3f}", "   blue", primaries->primary[2].x,
             primaries->primary[2].y);
    LOG_INFO("\t{}: {:.3f}, {:.3f}", "default white",
             primaries->default_white.x, primaries->default_white.y);
    LOG_INFO("default gamma: {:.2f}", di_info_get_default_gamma(info));

    const auto ssc = di_info_get_supported_signal_colorimetry(info);
    assert(ssc);
    if (ssc->bt2020_cycc)
      LOG_INFO("signal colorimetry: BT2020_cYCC");
    if (ssc->bt2020_ycc)
      LOG_INFO("signal colorimetry: BT2020_YCC");
    if (ssc->bt2020_rgb)
      LOG_INFO("signal colorimetry: BT2020_RGB");
    if (ssc->st2113_rgb)
      LOG_INFO("signal colorimetry: P3D65+P3DCI");
    if (ssc->ictcp)
      LOG_INFO("signal colorimetry: ICtCp");
  }

  static void print_edid(const std::string& node) {
    std::filesystem::path edid_path(node);
    edid_path /= "edid";
    if (!exists(edid_path)) {
      LOG_ERROR("EDID file not found: {}", edid_path.c_str());
      return;
    }

    uintmax_t file_size{};
    try {
      file_size = std::filesystem::file_size(edid_path);
    } catch (const std::filesystem::filesystem_error& e) {
      LOG_ERROR("Error reading filesize: {}", e.what());
      return;
    }

    FILE* f = fopen(edid_path.c_str(), "r");
    if (!f) {
      DLOG_DEBUG("Failed to open file: {}", edid_path.c_str());
      return;
    }

    // Read EDID
    auto buffer = std::make_unique<uint8_t[]>(file_size);
    size_t size{};
    while (size < file_size) {
      const size_t bytes_read = fread(&buffer[size], 1, file_size - size, f);
      if (bytes_read == 0) {
        if (ferror(f)) {
          LOG_ERROR("fread failed");
          break;
        }
        if (feof(f)) {
          break;
        }
      }
      size += bytes_read;
    }
    fclose(f);

    // Parse EDID
    if (size) {
      const auto info = di_info_parse_edid(buffer.get(), size);
      if (!info) {
        LOG_ERROR("di_edid_parse failed");
        return;
      }

      LOG_INFO("==========================");
      LOG_INFO("EDID:");
      print_di_info(info);
      LOG_INFO("==========================");
      di_info_destroy(info);
    }
    buffer.reset();
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
        std::scoped_lock lock(cmd_mutex_);
        exit(EXIT_SUCCESS);
      }
      if (xdg_key_symbols[0] == XKB_KEY_d) {
        std::scoped_lock lock(cmd_mutex_);
        if (drmpp::utils::is_cmd_present("libinput")) {
          const std::string cmd = "libinput list-devices";
          if (std::string result; drmpp::utils::execute(cmd, result)) {
            LOG_INFO("{}", result);
          }
        }
      } else if (xdg_key_symbols[0] == XKB_KEY_b) {
        std::scoped_lock lock(cmd_mutex_);
        const auto nodes = drmpp::utils::get_enabled_drm_nodes();
        for (const auto& node : nodes) {
          std::string node_info = drmpp::info::DrmInfo::get_node_info(node);
          std::cout << node_info << std::endl;
        }
      } else if (xdg_key_symbols[0] == XKB_KEY_a) {
        std::scoped_lock lock(cmd_mutex_);
        drmpp::utils::get_udev_sys_attributes("drm");
        drmpp::utils::get_udev_sys_attributes("input");
        drmpp::utils::get_udev_sys_attributes("graphics");
        drmpp::utils::get_udev_sys_attributes("vtconsole");
        drmpp::utils::get_udev_sys_attributes("backlight");
      } else if (xdg_key_symbols[0] == XKB_KEY_e) {
        std::scoped_lock lock(cmd_mutex_);
        auto nodes = drmpp::utils::get_enabled_drm_output_nodes(true);
        for (const auto& node : nodes) {
          print_edid(node);
        }
      }
    } else if (xdg_key_symbols[0] == XKB_KEY_m) {
      std::scoped_lock lock(cmd_mutex_);
      for (const auto& node : drmpp::utils::get_enabled_drm_nodes()) {
        const auto drm_fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
        if (drm_fd < 0) {
          LOG_ERROR("Failed to open {}", node.c_str());
          break;
        }

        LOG_INFO("** {} **", node);
        if (drm->SetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
          LOG_ERROR("drmSetClientCap(ATOMIC)");
          break;
        }

        const auto drm_res = drm->ModeGetResources(drm_fd);
        for (auto i = 0; i < drm_res->count_connectors; i++) {
          const auto connector =
              drm->ModeGetConnector(drm_fd, drm_res->connectors[i]);
          if (connector->connection != DRM_MODE_CONNECTED) {
            continue;
          }
          LOG_INFO("\tid: {}", connector->connector_id);
          LOG_INFO("\ttype: {}", connector->connector_type);
          switch (connector->connection) {
            case DRM_MODE_CONNECTED:
              LOG_INFO("\tconnection: DRM_MODE_CONNECTED");
              break;
            case DRM_MODE_DISCONNECTED:
              LOG_INFO("\tconnection: DRM_MODE_DISCONNECTED");
              break;
            case DRM_MODE_UNKNOWNCONNECTION:
              LOG_INFO("\tconnection: DRM_MODE_UNKNOWNCONNECTION");
              break;
          }
          LOG_INFO("\tphy_width: {}", connector->mmWidth);
          LOG_INFO("\tphy_height: {}", connector->mmHeight);
          switch (connector->subpixel) {
            case DRM_MODE_SUBPIXEL_UNKNOWN:
              LOG_INFO("\tsubpixel: DRM_MODE_SUBPIXEL_UNKNOWN");
              break;
            case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
              LOG_INFO("\tsubpixel: DRM_MODE_SUBPIXEL_HORIZONTAL_RGB");
              break;
            case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
              LOG_INFO("\tsubpixel: DRM_MODE_SUBPIXEL_HORIZONTAL_BGR");
              break;
            case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
              LOG_INFO("\tsubpixel: DRM_MODE_SUBPIXEL_VERTICAL_RGB");
              break;
            case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
              LOG_INFO("\tsubpixel: DRM_MODE_SUBPIXEL_VERTICAL_BGR");
              break;
            case DRM_MODE_SUBPIXEL_NONE:
              LOG_INFO("\tsubpixel: DRM_MODE_SUBPIXEL_NONE");
              break;
          }
          LOG_INFO("\tencoder_id: {}", connector->encoder_id);

          for (int j = 0; j < connector->count_modes; ++j) {
            const drmModeModeInfo* mode = &connector->modes[j];
            LOG_INFO("\t* {}", mode->name);
            LOG_INFO("\t\tclock: {}", mode->clock);
            LOG_INFO("\t\thdisplay: {}", mode->hdisplay);
            LOG_INFO("\t\thsync_start: {}", mode->hsync_start);
            LOG_INFO("\t\thsync_end: {}", mode->hsync_end);
            LOG_INFO("\t\thtotal: {}", mode->htotal);
            LOG_INFO("\t\thskew: {}", mode->hskew);
            LOG_INFO("\t\tvdisplay: {}", mode->vdisplay);
            LOG_INFO("\t\tvsync_start: {}", mode->vsync_start);
            LOG_INFO("\t\tvsync_end: {}", mode->vsync_end);
            LOG_INFO("\t\tvtotal: {}", mode->vtotal);
            LOG_INFO("\t\tvscan: {}", mode->vscan);
            LOG_INFO("\t\tvrefresh: {}", mode->vrefresh);
            LOG_INFO("\t\tflags: {}", mode->flags);
            LOG_INFO("\t\ttype: {}", mode->type);
          }
          drm->ModeFreeConnector(connector);
        }
        drm->ModeFreeResources(drm_res);
      }
    }
    LOG_INFO(
        "Key: time: {}, xkb_scancode: 0x{:X}, key_repeats: {}, state: {}, "
        "xdg_keysym_count: {}, syms_out[0]: 0x{:X}",
        time, xkb_scancode, keymap_key_repeats,
        state == LIBINPUT_KEY_STATE_PRESSED ? "press" : "release",
        xdg_key_symbol_count, xdg_key_symbols[0]);
  }

 private:
  std::unique_ptr<Logging> logging_;
  std::unique_ptr<drmpp::input::Seat> seat_;
  std::mutex cmd_mutex_{};
};

int main(const int argc, char** argv) {
  std::signal(SIGINT, handle_signal);

  cxxopts::Options options("drm-input", "Input information");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()("help", "Print help");

  if (options.parse(argc, argv).count("help")) {
    LOG_INFO("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  const App app({});

  while (gRunning && app.run()) {
  }

  return EXIT_SUCCESS;
}
