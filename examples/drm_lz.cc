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
#include <input/fastlz.h>

#include "drmpp.h"

#include "input/xkeymap_us_pc105.h"

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

	[[nodiscard]] bool run() {
		constexpr auto filesize = static_cast<double>(std::size(kXKeymap_us_pc105));
		auto *compressed_buffer = static_cast<uint8_t *>(malloc(1.05 * std::size(kXKeymap_us_pc105)));
		int compressed_size = fastlz_compress_level(1, kXKeymap_us_pc105, std::size(kXKeymap_us_pc105),
		                                            compressed_buffer);
		double ratio = (100.0 * compressed_size) / filesize;

		std::stringstream ss;
		ss << drmpp::utils::CustomHexHeaderdump<19>(compressed_buffer, compressed_size);

		LOG_INFO("Compressed size:   {}", compressed_size);
		LOG_INFO("Uncompressed size: {}", std::size(kXKeymap_us_pc105));
		LOG_INFO("Ratio:             {}", ratio);
		LOG_INFO("\n{}", ss.str().c_str());

		free(compressed_buffer);

		return false;
	}

private:
	std::unique_ptr<Logging> logging_;
};

int main(const int argc, char **argv) {
	std::signal(SIGINT, handle_signal);

	cxxopts::Options options("drm-lz77", "Simple CODEC for keymap files");
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
