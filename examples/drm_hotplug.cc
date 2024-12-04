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

#include <unistd.h>
#include <csignal>
#include <filesystem>
#include <iostream>

#include <cxxopts.hpp>

#include "drmpp/utils/utils.h"

struct Configuration {};

static volatile bool gRunning = true;

class App final : public Logging {
 public:
  explicit App(const Configuration& /* config */) {}

  ~App() override = default;

  [[nodiscard]] static bool run() {
    const auto udev = udev_new();

    const auto mon = udev_monitor_new_from_netlink(udev, "udev");
    udev_monitor_filter_add_match_subsystem_devtype(mon, "drm", nullptr);
    udev_monitor_enable_receiving(mon);
    const auto fd = udev_monitor_get_fd(mon);

    while (gRunning) {
      fd_set fds;
      FD_ZERO(&fds);
      FD_SET(fd, &fds);

      timeval tv{};
      tv.tv_sec = 0;
      tv.tv_usec = 0;

      // non-blocking
      if (const int ret = select(fd + 1, &fds, nullptr, nullptr, &tv);
          ret > 0 && FD_ISSET(fd, &fds)) {
        if (const auto dev = udev_monitor_receive_device(mon)) {
          auto node = udev_device_get_devnode(dev);
          auto subsystem = udev_device_get_subsystem(dev);
          auto devtype = udev_device_get_devtype(dev);
          auto action = udev_device_get_action(dev);
          LOG_INFO("Got Device");
          LOG_INFO("   Node: {}", node ? node : "");
          LOG_INFO("   Subsystem: {}", subsystem ? subsystem : "");
          LOG_INFO("   Devtype: {}", devtype ? devtype : "");
          LOG_INFO("   Action: {}", action ? action : "");
          udev_device_unref(dev);

          auto nodes = drmpp::utils::get_enabled_drm_nodes();
          LOG_INFO("Enabled:");
          for (const auto& n : nodes) {
            LOG_INFO("\t{}", n);
          }
        } else {
          LOG_INFO("No Device from receive_device(). An error occured.");
        }
      }
      usleep(250 * 1000);
      fflush(stdout);
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

  cxxopts::Options options("drm-hotplug", "monitor drm hotplug events");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()("help", "Print help");

  if (options.parse(argc, argv).count("help")) {
    LOG_INFO("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  const App app({});

  (void)App::run();

  return EXIT_SUCCESS;
}
