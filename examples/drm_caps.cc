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

#include "drmpp/info/info.h"
#include "drmpp/utils/utils.h"

struct Configuration {};

static volatile bool gRunning = true;

class App final : public Logging {
 public:
  explicit App(const Configuration& /* config */) {}

  ~App() override = default;

  [[nodiscard]] static bool run() {
    const auto nodes = drmpp::utils::get_drm_nodes();
    const std::string node_info = drmpp::info::DrmInfo::get_node_info(nodes);
    std::cout << node_info << std::endl;
    return false;
  }
};

int main(const int argc, char** argv) {
  std::signal(SIGINT, [](const int signal) {
    if (signal == SIGINT) {
      gRunning = false;
    }
  });

  cxxopts::Options options("drm-caps", "DRM driver caps to JSON");
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
