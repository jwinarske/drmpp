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
#include <fstream>
#include <iostream>

#include <libinput.h>

#include <cxxopts.hpp>
#include <utils.h>
#include <info/info.h>
#include <input/keyboard.h>

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

class App final : public drmpp::input::KeyboardObserver, public drmpp::input::SeatObserver {
public:
    explicit App(const Configuration & /*config */)
        : logging_(std::make_unique<Logging>()) {
        seat_ = std::make_unique<drmpp::input::Seat>(false, "");
        seat_->register_observer(this, this);
    }

    ~App() override {
        seat_.reset();
    }

    [[nodiscard]] bool run() const {
        return seat_->run_once();
    }

    void notify_seat_capabilities(drmpp::input::Seat *seat,
                                  uint32_t caps) override {
        LOG_INFO("Seat Capabilities: {}", caps);
        if (caps &= SEAT_CAPABILITIES_KEYBOARD) {
            auto keyboard = seat_->get_keyboard();
            if (keyboard.has_value()) {
                keyboard.value()->register_observer(this, this);
            }
        }
    }

    void notify_keyboard_xkb_v1_key(
        drmpp::input::Keyboard *keyboard,
        uint32_t time,
        uint32_t xkb_scancode,
        bool keymap_key_repeats,
        const uint32_t state,
        int xdg_key_symbol_count,
        const xkb_keysym_t *xdg_key_symbols) override {
        if (state == LIBINPUT_KEY_STATE_PRESSED) {
            if (xdg_key_symbols[0] == XKB_KEY_Escape) {
                std::scoped_lock<std::mutex> lock(cmd_mutex_);
                exit(EXIT_SUCCESS);
            } else if (xdg_key_symbols[0] == XKB_KEY_d) {
                std::scoped_lock<std::mutex> lock(cmd_mutex_);
                if (drmpp::utils::is_cmd_present("libinput")) {
                    const std::string cmd = "libinput list-devices";
                    std::string result;
                    if (drmpp::utils::execute(cmd.c_str(), result)) {
                        LOG_INFO("{}", result);
                    }
                }
            } else if (xdg_key_symbols[0] == XKB_KEY_b) {
                std::scoped_lock<std::mutex> lock(cmd_mutex_);
                const std::string path = "/dev/dri";
                for (const auto &entry: std::filesystem::directory_iterator(path)) {
                    if (entry.path().string().find("card") != std::string::npos) {
                        std::string node_info = drmpp::info::DrmInfo::get_node_info(entry.path().c_str());
                        std::cout << node_info << std::endl;
                    }
                }
            } else if (xdg_key_symbols[0] == XKB_KEY_p) {
                std::scoped_lock<std::mutex> lock(cmd_mutex_);
                if (drmpp::utils::is_cmd_present("udevadm")) {
                    const std::string path = "/dev/input/by-path";
                    for (const auto &entry: std::filesystem::directory_iterator(path)) {
                        auto device_name = read_symlink(entry).generic_string();

                        std::string token = "../";
                        auto i = device_name.find(token);
                        if (i != std::string::npos) {
                            device_name.erase(i, token.length());
                        }

                        LOG_INFO("{}:\t{}}", entry.path().generic_string(), device_name);
                        std::string cmd = "udevadm test $(udevadm info -q path -n /dev/input/" + device_name + ")";
                        std::string result;
                        if (!drmpp::utils::execute(cmd.c_str(), result)) {
                            LOG_ERROR("failed to query /dev/input/{}", device_name);
                            continue;
                        }
                        LOG_INFO("Input Device: {}\n{}", device_name, result);
                    }
                }
            }
        } else if (xdg_key_symbols[0] == XKB_KEY_e) {
            std::scoped_lock<std::mutex> lock(cmd_mutex_);
            if (drmpp::utils::is_cmd_present("find")) {
                std::string result;
                if (!drmpp::utils::execute("find /sys/devices -iname edid", result)) {
                    LOG_ERROR("Failed to find edid");
                    return;
                }
                auto condidates = drmpp::utils::split(result, "\n");
                for (const auto &candidate: condidates) {
                    if (candidate.empty()) {
                        continue;
                    }
                    FILE *f = fopen(candidate.c_str(), "r");
                    if (!f) {
                        DLOG_DEBUG("Failed to load file: {}", candidate);
                        return;
                    }

                    static uint8_t raw[32 * 1024];
                    size_t size{};
                    while (!feof(f)) {
                        size += fread(&raw[size], 1, sizeof(raw) - size, f);
                        if (ferror(f)) {
                            LOG_ERROR("fread failed");
                            break;
                        }
                        if (size >= sizeof(raw)) {
                            fprintf(stderr, "input too large\n");
                            break;
                        }
                    }
                    fclose(f);

                    if (size) {
                        std::stringstream ss;
                        ss << drmpp::utils::Hexdump(raw, size);
                        LOG_INFO("[{}]\n{}", candidate, ss.str());
                    }
                }
            }
        }
        LOG_INFO(
            "Key: time: {}, xkb_scancode: 0x{:X}, key_repeats: {}, state: {}, xdg_keysym_count: {}, syms_out[0]: 0x{:X}",
            time, xkb_scancode, keymap_key_repeats,
            state == LIBINPUT_KEY_STATE_PRESSED ? "press" : "release",
            xdg_key_symbol_count, xdg_key_symbols[0]);
    }

private:
    std::unique_ptr<Logging> logging_;
    std::unique_ptr<drmpp::input::Seat> seat_;
    std::mutex cmd_mutex_{};
};

int main(const int argc, char **argv) {
    std::signal(SIGINT, handle_signal);

    cxxopts::Options options("drm-input", "Input information");
    options.set_width(80)
            .set_tab_expansion()
            .allow_unrecognised_options()
            .add_options()("help", "Print help");

    const auto result = options.parse(argc, argv);

    if (result.count("help")) {
        spdlog::info("{}", options.help({"", "Group"}));
        exit(EXIT_SUCCESS);
    }

    App app({});

    while (gRunning && app.run()) {
    }

    return EXIT_SUCCESS;
}
