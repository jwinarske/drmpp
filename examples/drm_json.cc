/*
* Copyright 2024 drmpp contributors
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

#include "drmpp.h"

#include "info/info.h"


struct Configuration {
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

class App final {
public:
    explicit App(const Configuration & /* config */)
        : logging_(std::make_unique<Logging>()) {
    }

    ~App() = default;

    [[nodiscard]] bool run() const {
        const std::string path = "/dev/dri";
        for (const auto &entry: std::filesystem::directory_iterator(path)) {
            if (entry.path().string().find("card") != std::string::npos) {
                std::string node_info = drmpp::kms::info::DrmInfo::get_node_info(entry.path().c_str());
                std::cout << node_info << std::endl;
            }
        }
        return false;
    }

private:
    std::unique_ptr<Logging> logging_;
};

int main(const int argc, char **argv) {
    std::signal(SIGINT, handle_signal);

    cxxopts::Options options("drm-json", "DRM info to JSON");
    options.set_width(80)
            .set_tab_expansion()
            .allow_unrecognised_options()
            .add_options()("help", "Print help");

    const auto result = options.parse(argc, argv);

    if (result.count("help")) {
        spdlog::info("{}", options.help({"", "Group"}));
        exit(EXIT_SUCCESS);
    }

    const App app({});

    while (gRunning && app.run()) {
    }

    return EXIT_SUCCESS;
}
