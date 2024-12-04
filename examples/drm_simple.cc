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

#include <drm_fourcc.h>
#include "shared_libs/libdrm.h"

#include <cxxopts.hpp>

#include "drmpp.h"
#include "utils/utils.h"
#include "utils/virtual_terminal.h"

struct Configuration {
  bool quit = false;
};

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

class App final : public drmpp::utils::VirtualTerminal {
 public:
  explicit App(const Configuration& config)
      : logging_(std::make_unique<Logging>()), config_(config) {}

  ~App() override = default;

  [[nodiscard]] bool run() const {
    for (const auto& node : drmpp::utils::get_enabled_drm_nodes()) {
      auto device = drmpp::Device::open(node);

      auto output = device->openFirstConnectedOutput();

      /// TODO: Maybe disable other CRTCs.

      LOG_INFO("Using connector {}, CRTC {}", output->connector_id(),
               output->crtc_id());

      auto mode = output->mode();
      LOG_INFO("Mode: {}x{}@{}Hz", mode.hdisplay, mode.vdisplay,
               output->refreshRate());

      output->present(generateFrame(*device, mode.hdisplay, mode.vdisplay));

      if (!config_.quit) {
        sleep(1);
      }
    }

    return false;
  }

private:
  std::unique_ptr<Logging> logging_;
  const Configuration config_;

  static constexpr uint32_t kLayersLen = UINT32_C(4);

  /* ARGB 8:8:8:8 */
  static constexpr uint32_t kColors[] = {
    0xFFFF0000, /* red */
    0xFF00FF00, /* green */
    0xFF0000FF, /* blue */
    0xFFFFFF00, /* yellow */
  };

  static void add_layer(drmpp::Device& device,
                        drmpp::Composition& composition,
                        int x,
                        int y,
                        uint32_t width,
                        uint32_t height,
                        bool with_alpha) {
    static bool first = true;
    static size_t color_idx = 0;

    std::shared_ptr<drmpp::Buffer> fb_shared;
    {
      uint64_t modifier = DRM_FORMAT_MOD_INVALID;

      // If we can, explicitly specify a linear layout
      if (device.supportsModifiers()) {
        modifier = DRM_FORMAT_MOD_LINEAR;
      }

      auto fb = device.createDumbBuffer(
          width, height, 32,
          with_alpha ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_XRGB8888, modifier);
      if (!fb) {
        LOG_ERROR("Failed to create dumb buffer");
        return;
      }

      fb_shared = std::move(fb);
    }

    LOG_INFO("Created dumb buffer with size {}x{}", width, height);

    uint32_t color;
    if (first) {
      color = 0xFFFFFFFF;
      first = false;
    } else {
      color = kColors[color_idx];
      color_idx = (color_idx + 1) % std::size(kColors);
    }

    fb_shared->fill(color);

    const auto layer = drmpp::Composition::Layer{
        fb_shared,
        {0, 0, width, height},
        {static_cast<uint32_t>(x), static_cast<uint32_t>(y), width, height},
    };

    composition.addLayer(layer);
  }

  static drmpp::Composition generateFrame(drmpp::Device& device,
                                               int width,
                                               int height) {
    drmpp::Composition composition;

    add_layer(device, composition, 0, 0, width, height, false);
    for (uint32_t i = 1; i < kLayersLen; i++) {
      add_layer(device, composition, UINT32_C(100) * i, UINT32_C(100) * i, 256,
                256, i % 2);
    }

    return composition;
  }
};

int main(const int argc, char **argv) {
  std::signal(SIGINT, handle_signal);

  liftoff_log_set_priority(LIFTOFF_DEBUG);

  cxxopts::Options options("drm-simple", "Simple DRM example");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()("help", "Print help")(
          "quit", "Quit directly after applying the commit.");

  const auto parsed = options.parse(argc, argv);
  if (parsed.count("help")) {
    spdlog::info("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  App app({
      .quit = !!parsed.count("quit"),
  });

  (void)app.run();

  return EXIT_SUCCESS;
}
