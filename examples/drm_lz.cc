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
#include <fstream>

#include <cxxopts.hpp>
#include <input/fastlz.h>

#include "drmpp.h"

struct Configuration {
	std::string keymap_path;
	std::string left_ptr_cursor_path;
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

class App {
public:
	explicit App(const Configuration &config)
		: logging_(std::make_unique<Logging>()), config_(config) {
	}

	~App() = default;


	static std::vector<uint8_t> create_buffer(std::ifstream &file,
	                                          const std::size_t size) {
		std::vector<uint8_t> buffer(size);
		if (!file.read(reinterpret_cast<char *>(buffer.data()),
		               static_cast<long>(buffer.size()))) {
			buffer.clear();
		}
		return buffer;
	}

	static std::optional<std::ifstream> read_file_content(
		const std::filesystem::path &path) {
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open()) {
			LOG_ERROR("[{}] Failed to open", path.c_str());
			return {};
		}
		return file;
	}

	static bool is_valid_file_path(const std::filesystem::path &path) {
		if (path.empty() || !exists(path)) {
			LOG_ERROR("invalid path: {}", path.c_str());
			return false;
		}
		return true;
	}

	static std::vector<uint8_t> return_error_message_and_buffer(
		const std::filesystem::path &path,
		const std::string &message) {
		LOG_ERROR("[{}] {}", path.c_str(), message);
		return {};
	}

	static std::vector<uint8_t> read_binary_file(const std::string &file_path) {
		if (!is_valid_file_path(file_path)) {
			return return_error_message_and_buffer(file_path, "Invalid path");
		}

		std::optional<std::ifstream> optionalFile = read_file_content(file_path);
		if (!optionalFile.has_value()) {
			return return_error_message_and_buffer(file_path, "Failed to open");
		}

		std::ifstream &file = optionalFile.value();
		const auto end = file.tellg();
		file.seekg(0, std::ios::beg);
		const auto size = static_cast<std::size_t>(end - file.tellg());

		SPDLOG_TRACE("Read: {} bytes", size);
		if (size == 0) {
			return return_error_message_and_buffer(file_path, "Empty file");
		}

		std::vector<uint8_t> buffer = create_buffer(file, size);
		if (buffer.empty()) {
			return return_error_message_and_buffer(file_path, "Failed to read");
		}

		return buffer;
	}

	static bool get_asset_header(const std::string &asset) {
		LOG_INFO("** {} **", asset.c_str());
		const auto buff = read_binary_file(asset);

		std::stringstream ss;
		auto *compressed_buffer = static_cast<uint8_t *>(malloc(1.05 * buff.size()));
		int compressed_size = fastlz_compress_level(1, buff.data(), buff.size(), compressed_buffer);
		double ratio1 = (100.0 * compressed_size) / buff.size();

		ss.clear();
		ss.str("");
		ss << drmpp::utils::CustomHexHeaderdump<19>(compressed_buffer, compressed_size);

		LOG_INFO("Compressed size:   {}", compressed_size);
		LOG_INFO("Uncompressed size: {}", buff.size());
		LOG_INFO("Ratio:             {}", ratio1);
		LOG_INFO("\n{}", ss.str().c_str());

		free(compressed_buffer);
		LOG_INFO("********");
		return true;
	}

	[[nodiscard]] bool run() {
		get_asset_header(config_.keymap_path);
		get_asset_header(config_.left_ptr_cursor_path);
		return false;
	}

private:
	std::unique_ptr<Logging> logging_;
	const Configuration &config_;
};

int main(const int argc, char **argv) {
	std::signal(SIGINT, handle_signal);

	Configuration config{};
	cxxopts::Options options("drm-lz", "Generate compressed header content");
	options.set_width(80)
			.set_tab_expansion()
			.allow_unrecognised_options()
			.add_options()("help", "Print help")
			("k,keymap", "Path to keymap file",
			 cxxopts::value<std::string>(config.keymap_path))
			("c,cursor", "Path to left_ptr (default) cursor file",
			 cxxopts::value<std::string>(config.left_ptr_cursor_path));
	const auto result = options.parse(argc, argv);

	if (result.count("help")) {
		spdlog::info("{}", options.help({"", "Group"}));
		exit(EXIT_SUCCESS);
	}
	if (!result.count("keymap")) {
		const auto home = getenv("HOME");
		if (!home) {
			LOG_ERROR("HOME is not set.");
			exit(EXIT_FAILURE);
		}
		config.keymap_path = std::string(home) + "/.xkb/keymap.xkb";
	}
	if (!result.count("cursor")) {
		config.left_ptr_cursor_path = "/usr/share/icons/DMZ-White/cursors/left_ptr";
	}

	App app(config);

	while (gRunning && app.run()) {
	}

	return EXIT_SUCCESS;
}
