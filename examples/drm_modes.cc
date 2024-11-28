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

#include <cxxopts.hpp>

#include "drmpp/input/seat.h"
#include "drmpp/shared_libs/libdrm.h"
#include "drmpp/utils/utils.h"

struct Configuration {};

static volatile bool gRunning = true;

class App final : public Logging {
 public:
  explicit App(const Configuration& /* config */) {}

  ~App() override = default;

  [[nodiscard]] static bool run() {
    for (const auto& node : drmpp::utils::get_enabled_drm_nodes()) {
      const auto drm_fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
      if (drm_fd < 0) {
        LOG_ERROR("Failed to open {}", node.c_str());
        return false;
      }

      LOG_INFO("** {} **", node);
      if (drm->SetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
        LOG_ERROR("drmSetClientCap(ATOMIC)");
        return false;
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
    return false;
  }
};

int main(const int argc, char** argv) {
  std::signal(SIGINT, [](const int signal) {
    if (signal == SIGINT) {
      gRunning = false;
    }
  });

  cxxopts::Options options("drm-modes", "DRM modes");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()("help", "Print help");

  if (options.parse(argc, argv).count("help")) {
    spdlog::info("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  const App app({});

  (void)App::run();

  return EXIT_SUCCESS;
}
