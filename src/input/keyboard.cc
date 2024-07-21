
#include "input/keyboard.h"

#include <filesystem>

#include <cstdio>
#include <sys/mman.h>
#include <ctime>
#include <iostream>
#include <utils.h>

#include "info/info.h"

#include "drmpp.h"

namespace drmpp::input {
    Keyboard::Keyboard(const int32_t delay, const int32_t repeat) {
        xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
        xkb_context_set_log_verbosity(xkb_context_, XKB_LOG_LEVEL_INFO);
        load_keymap();
        handle_repeat_info(delay, repeat);
    }

    Keyboard::~Keyboard() {
        if (xkb_state_) {
            xkb_state_unref(xkb_state_);
        }
        if (xkb_keymap_) {
            xkb_keymap_unref(xkb_keymap_);
        }
        if (xkb_context_) {
            xkb_context_unref(xkb_context_);
        }
    }

    void Keyboard::load_keymap(const char *keymap_file) {
        std::string file;
        if (!keymap_file) {
            if (!utils::is_cmd_present("xkbcomp")) {
                LOG_ERROR("xkbcomp is required to create keymap file");
                return;
            }

            std::filesystem::path xkb_folder = getenv("HOME");
            if (xkb_folder.empty()) {
                LOG_CRITICAL("$HOME is not set.  Failed to load keymap");
                exit(EXIT_FAILURE);
            }

            xkb_folder /= ".xkb";
            if (!exists(xkb_folder)) {
                create_directory(xkb_folder);
            }

            if (!utils::is_cmd_present("xkbcomp")) {
                LOG_CRITICAL("xkbcomp is required to create the keymap file");
                exit(EXIT_FAILURE);
            }

            const std::string display = getenv("DISPLAY");
            if (xkb_folder.empty()) {
                LOG_CRITICAL("$DISPLAY is not set");
                exit(EXIT_FAILURE);
            }

            const std::string cmd = "xkbcomp " + display + " " + xkb_folder.string() + "/keymap.xkb";

            std::string result;
            if (!utils::execute(cmd.c_str(), result)) {
                LOG_WARN("Failed to create keymap file");
            }
            file = xkb_folder.string() + "/keymap.xkb";
        } else {
            file = keymap_file;
        }

        DLOG_DEBUG("Loading keymap file: {}", file);
        FILE *f = fopen(file.c_str(), "r");
        if (!f) {
            DLOG_DEBUG("Failed to load file: {}", file);
            return;
        }
        xkb_keymap_unref(xkb_keymap_);
        xkb_keymap_ = xkb_keymap_new_from_file(xkb_context_, f, XKB_KEYMAP_FORMAT_TEXT_V1,
                                               XKB_KEYMAP_COMPILE_NO_FLAGS);
        fclose(f);
        xkb_state_unref(xkb_state_);
        xkb_state_ = xkb_state_new(xkb_keymap_);
    }

    void Keyboard::handle_key(libinput_event_keyboard *key_event) {
        const auto key = libinput_event_keyboard_get_key(key_event);
        const auto state = libinput_event_keyboard_get_key_state(key_event);
        auto time = libinput_event_keyboard_get_time(key_event);

        /// translate scancode to XKB scancode
        const auto xkb_scancode = key + 8;
        const auto key_repeats = xkb_keymap_key_repeats(xkb_keymap_, xkb_scancode);

        const xkb_keysym_t *key_syms;
        const auto xdg_keysym_count =
                xkb_state_key_get_syms(xkb_state_, xkb_scancode, &key_syms);

        if (key_repeats) {
            // start/restart timer
            itimerspec in{};
            in.it_value.tv_nsec = repeat_.delay * 1000000;
            in.it_interval.tv_nsec = repeat_.rate * 1000000;
            timer_settime(repeat_.timer, 0, &in, nullptr);

            // update notify values
            repeat_.notify = {
                .time = time,
                .xkb_scancode = xkb_scancode,
                .key_repeats = key_repeats,
                .xdg_keysym_count = xdg_keysym_count,
                .key_syms = key_syms
            };
        }
        if (state == LIBINPUT_KEY_STATE_PRESSED) {
            if (key_syms[0] == XKB_KEY_Escape) {
                exit(EXIT_SUCCESS);
            } else if (key_syms[0] == XKB_KEY_d) {
                if (utils::is_cmd_present("libinput")) {
                    const std::string cmd = "libinput list-devices";
                    std::string result;
                    if (utils::execute(cmd.c_str(), result)) {
                        LOG_INFO("{}", result);
                    }
                }
            } else if (key_syms[0] == XKB_KEY_b) {
                const std::string path = "/dev/dri";
                for (const auto &entry: std::filesystem::directory_iterator(path)) {
                    if (entry.path().string().find("card") != std::string::npos) {
                        std::string node_info = kms::info::DrmInfo::get_node_info(entry.path().c_str());
                        std::cout << node_info << std::endl;
                    }
                }
            } else if (key_syms[0] == XKB_KEY_u) {
                if (utils::is_cmd_present("udevadm")) {
                    const std::string path = "/dev/input/by-path";
                    for (const auto &entry: std::filesystem::directory_iterator(path)) {
                        auto device_name = read_symlink(entry).generic_string();

                        std::string token = "../";
                        auto i = device_name.find(token);
                        if (i != std::string::npos) {
                            device_name.erase(i, token.length());
                        }

                        LOG_INFO("{}:\t{}}", entry.path().generic_string(), device_name);

                        std::string cmd =
                                "udevadm info --attribute-walk --path=$(udevadm info --query=path --name=/dev/input/" +
                                device_name + ")";
                        std::string result;
                        if (!utils::execute(cmd.c_str(), result)) {
                            LOG_ERROR("failed to query /dev/input/{}", device_name);
                            continue;
                        }
                        LOG_INFO("Input Device: {}\n{}", device_name, result);
                    }
                }
            }
        } else if (state == LIBINPUT_KEY_STATE_RELEASED) {
            if (repeat_.notify.xkb_scancode == xkb_scancode) {
                // stop timer
                itimerspec its{};
                timer_settime(repeat_.timer, 0, &its, nullptr);
            }
        }

        LOG_INFO(
            "Key: time: {}, xkb_scancode: 0x{:X}, key_repeats: {}, state: {}, xdg_keysym_count: {}, syms_out[0]: 0x{:X}",
            time, xkb_scancode, key_repeats,
            state == LIBINPUT_KEY_STATE_PRESSED ? "press" : "release",
            xdg_keysym_count, key_syms[0]);
    }

    void Keyboard::handle_repeat_info(const int32_t delay, const int32_t rate) {
        repeat_.rate = rate;
        repeat_.delay = delay;

        if (!repeat_.timer) {
            /// Setup signal event
            repeat_.sev.sigev_notify = SIGEV_SIGNAL;
            repeat_.sev.sigev_signo = SIGRTMIN;
            repeat_.sev.sigev_value.sival_ptr = this;
            auto res =
                    timer_create(CLOCK_REALTIME, &repeat_.sev, &repeat_.timer);
            if (res != 0) {
                LOG_CRITICAL("Error timer_create: {}", std::strerror(errno));
                abort();
            }

            /// Setup signal action
            repeat_.sa.sa_flags = SA_SIGINFO;
            repeat_.sa.sa_sigaction = repeat_xkb_v1_key_callback;
            sigemptyset(&repeat_.sa.sa_mask);
            if (sigaction(SIGRTMIN, &repeat_.sa, nullptr) == -1) {
                LOG_CRITICAL("Error sigaction: {}", std::strerror(errno));
                abort();
            }
        }
    }

    void Keyboard::repeat_xkb_v1_key_callback(int /* sig */, siginfo_t *si, void * /* uc */) {
        const auto obj =
                static_cast<Keyboard *>(si->_sifields._rt.si_sigval.sival_ptr);

        LOG_INFO(
            "Key: time: {}, xkb_scancode: 0x{:X}, key_repeats: {}, state: {}, xdg_keysym_count: {}, syms_out[0]: 0x{:X}",
            obj->repeat_.notify.time, obj->repeat_.notify.xkb_scancode, obj->repeat_.notify.key_repeats, "pressed",
            obj->repeat_.notify.xdg_keysym_count, obj->repeat_.notify.key_syms[0]);
    }

    std::pair<std::string, std::string> Keyboard::get_keymap_filepath() {
        std::string keymap_dir = "/usr/share/X11/xkb/symbols/";
        if (!std::filesystem::exists(keymap_dir)) {
            keymap_dir = "/usr/X11/xkb/symbols/";
            if (!std::filesystem::exists(keymap_dir)) {
                LOG_WARN("xkb keymaps are not installed");
                return {};
            }
        }

        std::string xkb_layout;
        std::string xkb_variant;

        // try with localectl first in case of default override
        if (utils::is_cmd_present("localectl")) {
            constexpr char cmd[] = "localectl status";
            std::string result;
            if (!utils::execute(cmd, result)) {
                LOG_ERROR("Failed to run {}", cmd);
                return {};
            }
            auto lines = utils::split(result, "\n");
            for (auto const &line: lines) {
                DLOG_TRACE("Line: [{}]", line);
                if (line.find("X11 Layout") != std::string::npos) {
                    auto tokens = utils::split(line, ":");
                    xkb_layout = utils::trim(tokens[1], " ");
                    break;
                };
            }
            DLOG_TRACE("xkb_layout: [{}]", xkb_layout);
            DLOG_TRACE("xkb_variant: [{}]", xkb_variant);
            auto keymap_filepath = keymap_dir + xkb_layout;
            if (!std::filesystem::exists(keymap_filepath)) {
                LOG_ERROR("Keymap File does not exist: {}", keymap_filepath);
                return {};
            }
            return std::move(std::make_pair(keymap_filepath, xkb_variant));
        }

        // read the system default
        if (std::filesystem::exists("/etc/default/keyboard")) {
            constexpr char cmd[] = "cat /etc/default/keyboard";
            std::string result;
            if (!utils::execute(cmd, result)) {
                LOG_ERROR("Failed to run {}", cmd);
            }
            auto lines = utils::split(result, "\n");
            for (auto const &line: lines) {
                if (line.find("XKBLAYOUT=") != std::string::npos) {
                    auto tokens = utils::split(line, "=");
                    xkb_layout = utils::trim(tokens[1], "\"");
                } else if (line.find("XKBVARIANT=") != std::string::npos) {
                    auto tokens = utils::split(line, "=");
                    xkb_variant = utils::trim(tokens[1], "\"");
                };
            }
            DLOG_TRACE("xkb_layout: [{}]", xkb_layout);
            DLOG_TRACE("xkb_variant: [{}]", xkb_variant);
            auto keymap_filepath = keymap_dir + xkb_layout;
            if (!std::filesystem::exists(keymap_filepath)) {
                LOG_ERROR("Keymap File does not exist: {}", keymap_filepath);
                return {};
            }
            return std::move(std::make_pair(keymap_filepath, xkb_variant));
        }
        LOG_WARN("Not able to detect xkb keymap values");
        return {};
    }
}
