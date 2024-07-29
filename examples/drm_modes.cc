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


#include "drmpp.h"

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

	[[nodiscard]] static bool run() {
		for (const auto &node: drmpp::utils::get_enabled_drm_nodes(true)) {
			const auto drm_fd = open(node.c_str(), O_RDWR | O_CLOEXEC);
			if (drm_fd < 0) {
				LOG_ERROR("Failed to open {}", node.c_str());
				return false;
			}

			LOG_INFO("** {} **", node);
			if (drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1) < 0) {
				LOG_ERROR("drmSetClientCap(ATOMIC)");
				return false;
			}

			const auto drm_res = drmModeGetResources(drm_fd);
			const auto connector = drmpp::plane::Common::pick_connector(drm_fd, drm_res);
			if (connector == nullptr) {
				LOG_ERROR("no connector found");
				return false;
			}

			LOG_INFO("connector_id: {}", connector->connector_id);
			LOG_INFO("type: {}", connector->connector_type);
			switch (connector->connection) {
				case DRM_MODE_CONNECTED:
					LOG_INFO("connection: DRM_MODE_CONNECTED");
					break;
				case DRM_MODE_DISCONNECTED:
					LOG_INFO("connection: DRM_MODE_DISCONNECTED");
					break;
				case DRM_MODE_UNKNOWNCONNECTION:
					LOG_INFO("connection: DRM_MODE_UNKNOWNCONNECTION");
					break;
			}
			LOG_INFO("phy_width: {}", connector->mmWidth);
			LOG_INFO("phy_height: {}", connector->mmHeight);
			switch (connector->subpixel) {
				case DRM_MODE_SUBPIXEL_UNKNOWN:
					LOG_INFO("subpixel: DRM_MODE_SUBPIXEL_UNKNOWN");
					break;
				case DRM_MODE_SUBPIXEL_HORIZONTAL_RGB:
					LOG_INFO("subpixel: DRM_MODE_SUBPIXEL_HORIZONTAL_RGB");
					break;
				case DRM_MODE_SUBPIXEL_HORIZONTAL_BGR:
					LOG_INFO("subpixel: DRM_MODE_SUBPIXEL_HORIZONTAL_BGR");
					break;
				case DRM_MODE_SUBPIXEL_VERTICAL_RGB:
					LOG_INFO("subpixel: DRM_MODE_SUBPIXEL_VERTICAL_RGB");
					break;
				case DRM_MODE_SUBPIXEL_VERTICAL_BGR:
					LOG_INFO("subpixel: DRM_MODE_SUBPIXEL_VERTICAL_BGR");
					break;
				case DRM_MODE_SUBPIXEL_NONE:
					LOG_INFO("subpixel: DRM_MODE_SUBPIXEL_NONE");
					break;
			}
			LOG_INFO("encoder_id: {}", connector->encoder_id);

			for (int j = 0; j < connector->count_modes; ++j) {
				const drmModeModeInfo *mode = &connector->modes[j];
				LOG_INFO("* {} *", mode->name);
				LOG_INFO("\tclock: {}", mode->clock);
				LOG_INFO("\thdisplay: {}", mode->hdisplay);
				LOG_INFO("\thsync_start: {}", mode->hsync_start);
				LOG_INFO("\thsync_end: {}", mode->hsync_end);
				LOG_INFO("\thtotal: {}", mode->htotal);
				LOG_INFO("\thskew: {}", mode->hskew);
				LOG_INFO("\tvdisplay: {}", mode->vdisplay);
				LOG_INFO("\tvsync_start: {}", mode->vsync_start);
				LOG_INFO("\tvsync_end: {}", mode->vsync_end);
				LOG_INFO("\tvtotal: {}", mode->vtotal);
				LOG_INFO("\tvscan: {}", mode->vscan);
				LOG_INFO("\tvrefresh: {}", mode->vrefresh);
				LOG_INFO("\tflags: {}", mode->flags);
				LOG_INFO("\ttype: {}", mode->type);
			}

			drmModeFreeResources(drm_res);
			drmModeFreeConnector(connector);
		}
		return false;
	}

private:
	std::unique_ptr<Logging> logging_;

	static constexpr uint32_t kLayersLen = UINT32_C(4);

	/* ARGB 8:8:8:8 */
	static constexpr uint32_t kColors[] = {
		0xFFFF0000, /* red */
		0xFF00FF00, /* green */
		0xFF0000FF, /* blue */
		0xFFFFFF00, /* yellow */
	};

	static liftoff_layer *add_layer(const int drm_fd, liftoff_output *output, const int x, const int y,
	                                const uint32_t width, const uint32_t height, const bool with_alpha) {
		static bool first = true;
		static size_t color_idx = 0;
		drmpp::plane::Common::dumb_fb fb{};
		uint32_t color;
		liftoff_layer *layer{};

		if (!drmpp::plane::Common::dumb_fb_init(&fb, drm_fd, with_alpha ? DRM_FORMAT_ARGB8888 : DRM_FORMAT_XRGB8888,
		                                        width,
		                                        height)) {
			LOG_ERROR("failed to create framebuffer");
			return nullptr;
		}
		LOG_INFO("Created FB {} with size {}x{}", fb.id, width, height);

		if (first) {
			color = 0xFFFFFFFF;
			first = false;
		} else {
			color = kColors[color_idx];
			color_idx = (color_idx + 1) % std::size(kColors);
		}

		drmpp::plane::Common::dumb_fb_fill(&fb, drm_fd, color);

		layer = liftoff_layer_create(output);
		liftoff_layer_set_property(layer, "FB_ID", fb.id);
		liftoff_layer_set_property(layer, "CRTC_X", static_cast<uint64_t>(x));
		liftoff_layer_set_property(layer, "CRTC_Y", static_cast<uint64_t>(y));
		liftoff_layer_set_property(layer, "CRTC_W", width);
		liftoff_layer_set_property(layer, "CRTC_H", height);
		liftoff_layer_set_property(layer, "SRC_X", 0);
		liftoff_layer_set_property(layer, "SRC_Y", 0);
		liftoff_layer_set_property(layer, "SRC_W", width << 16);
		liftoff_layer_set_property(layer, "SRC_H", height << 16);

		return layer;
	}
};

int main(const int argc, char **argv) {
	std::signal(SIGINT, handle_signal);

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

	while (gRunning && app.run()) {
	}

	return EXIT_SUCCESS;
}
