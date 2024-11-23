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

#include "input/keyboard.h"

#include <filesystem>

#include <input/default_xkeymap.h>
#include <utils.h>
#include <cstdio>
#include <ctime>

#include "drmpp.h"

namespace drmpp::input {
Keyboard::Keyboard(event_mask const& event_mask,
                   const char* model,
                   const char* layout,
                   const char* variant,
                   const char* options,
                   const int32_t delay,
                   const int32_t repeat) {
  (void)model;
  (void)layout;
  (void)variant;
  (void)options;
  event_mask_ = {
      .enabled = event_mask.enabled,
      .all = event_mask.all,
  };
  xkb_context_ = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  xkb_context_set_log_verbosity(xkb_context_, XKB_LOG_LEVEL_INFO);
  load_default_keymap();
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

void Keyboard::register_observer(KeyboardObserver* observer, void* user_data) {
  std::scoped_lock lock(observers_mutex_);
  observers_.push_back(observer);

  if (user_data) {
    user_data_ = user_data;
  }
}

void Keyboard::unregister_observer(KeyboardObserver* observer) {
  std::scoped_lock lock(observers_mutex_);
  observers_.remove(observer);
}

void Keyboard::load_default_keymap() {
  if (xkb_keymap_) {
    xkb_keymap_unref(xkb_keymap_);
  }

  auto buffer = utils::decompress_asset(kXkeymap);
  xkb_keymap_ = xkb_keymap_new_from_buffer(
      xkb_context_, reinterpret_cast<const char*>(buffer.data()), buffer.size(),
      XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  buffer.clear();
  assert(xkb_keymap_);
  xkb_state_unref(xkb_state_);
  xkb_state_ = xkb_state_new(xkb_keymap_);
}

void Keyboard::load_keymap_from_file(const std::string& keymap_file) {
  std::string file;
  do {
    if (keymap_file.empty() == true) {
      std::filesystem::path xkb_folder = getenv("HOME");
      if (xkb_folder.empty() == true) {
        LOG_CRITICAL("$HOME is not set.  Failed to load keymap");
        exit(EXIT_FAILURE);
      }

      xkb_folder /= ".xkb";
      if (exists(xkb_folder) == false) {
        create_directory(xkb_folder);
      }

      const auto display = std::string(getenv("DISPLAY"));
      if (display.empty()) {
        xkb_folder /= "keymap.xkb";
        LOG_DEBUG("Loading {}", xkb_folder.string());
        file.assign(xkb_folder);
        break;
      }

      if (utils::is_cmd_present("xkbcomp") == false) {
        LOG_CRITICAL("xkbcomp is required to create the keymap file");
        exit(EXIT_FAILURE);
      }

      xkb_folder /= "keymap.xkb";
      const std::string cmd = "xkbcomp " + display + " " + xkb_folder.string();

      if (std::string result; !utils::execute(cmd, result)) {
        LOG_WARN("Failed to create keymap file");
      }
      file = xkb_folder.string();
    } else {
      file = keymap_file;
    }
  } while (false);

  DLOG_DEBUG("Loading keymap file: {}", file);
  FILE* f = fopen(file.c_str(), "r");
  if (!f) {
    DLOG_DEBUG("Failed to load file: {}", file);
    return;
  }
  xkb_keymap_unref(xkb_keymap_);
  xkb_keymap_ = xkb_keymap_new_from_file(
      xkb_context_, f, XKB_KEYMAP_FORMAT_TEXT_V1, XKB_KEYMAP_COMPILE_NO_FLAGS);
  fclose(f);
  xkb_state_unref(xkb_state_);
  xkb_state_ = xkb_state_new(xkb_keymap_);
}

void Keyboard::handle_keyboard_event(libinput_event_keyboard* key_event) {
  const auto key = libinput_event_keyboard_get_key(key_event);
  const auto state = libinput_event_keyboard_get_key_state(key_event);
  auto time = libinput_event_keyboard_get_time(key_event);

  /// translate scancode to XKB scancode
  const auto xkb_scancode = key + 8;
  const auto key_repeats = xkb_keymap_key_repeats(xkb_keymap_, xkb_scancode);

  const xkb_keysym_t* key_symbols;
  const auto xdg_keysym_count =
      xkb_state_key_get_syms(xkb_state_, xkb_scancode, &key_symbols);

  if (key_repeats) {
    // start/restart timer
    itimerspec in{};
    in.it_value.tv_nsec = repeat_.delay * 1000000;
    in.it_interval.tv_nsec = repeat_.rate * 1000000;
    timer_settime(repeat_.timer, 0, &in, nullptr);

    // update notify values
    repeat_.notify = {.time = time,
                      .xkb_scancode = xkb_scancode,
                      .key_repeats = key_repeats,
                      .xdg_keysym_count = xdg_keysym_count,
                      .key_syms = key_symbols};
  }

  if (state == LIBINPUT_KEY_STATE_RELEASED) {
    if (repeat_.notify.xkb_scancode == xkb_scancode) {
      // stop timer
      constexpr itimerspec its{};
      timer_settime(repeat_.timer, 0, &its, nullptr);
    }
  }

  DLOG_TRACE(
      "Key: time: {}, xkb_scancode: 0x{:X}, key_repeats: {}, state: {}, "
      "xdg_keysym_count: {}, syms_out[0]: 0x{:X}",
      time, xkb_scancode, key_repeats,
      state == LIBINPUT_KEY_STATE_PRESSED ? "press" : "release",
      xdg_keysym_count, key_symbols[0]);

  if (event_mask_.enabled && event_mask_.all) {
    return;
  }

  std::scoped_lock lock(observers_mutex_);
  for (const auto observer : observers_) {
    observer->notify_keyboard_xkb_v1_key(this, time, xkb_scancode, key_repeats,
                                         state, xdg_keysym_count, key_symbols);
  }
}

void Keyboard::handle_repeat_info(const int32_t delay, const int32_t rate) {
  repeat_.rate = rate;
  repeat_.delay = delay;

  if (!repeat_.timer) {
    /// Setup signal event
    repeat_.sev.sigev_notify = SIGEV_SIGNAL;
    repeat_.sev.sigev_signo = SIGRTMIN;
    repeat_.sev.sigev_value.sival_ptr = this;
    if (const auto res =
            timer_create(CLOCK_REALTIME, &repeat_.sev, &repeat_.timer);
        res != 0) {
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

void Keyboard::repeat_xkb_v1_key_callback(int /* sig */,
                                          siginfo_t* si,  // NOLINT
                                          void* /* uc */) {
  const auto obj =
      static_cast<Keyboard*>(si->_sifields._rt.si_sigval.sival_ptr);

  LOG_INFO(
      "Key: time: {}, xkb_scancode: 0x{:X}, key_repeats: {}, state: {}, "
      "xdg_keysym_count: {}, syms_out[0]: 0x{:X}",
      obj->repeat_.notify.time, obj->repeat_.notify.xkb_scancode,
      obj->repeat_.notify.key_repeats, "pressed",
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
    for (auto const& line : lines) {
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
    return std::make_pair(keymap_filepath, xkb_variant);
  }

  // read the system default
  if (std::filesystem::exists("/etc/default/keyboard")) {
    constexpr char cmd[] = "cat /etc/default/keyboard";
    std::string result;
    if (!utils::execute(cmd, result)) {
      LOG_ERROR("Failed to run {}", cmd);
    }
    auto lines = utils::split(result, "\n");
    for (auto const& line : lines) {
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
    return std::make_pair(keymap_filepath, xkb_variant);
  }
  LOG_WARN("Not able to detect xkb keymap values");
  return {};
}

void Keyboard::set_event_mask(event_mask const& event_mask) {
  event_mask_.enabled = event_mask.enabled;
  event_mask_.all = event_mask.all;
}
}  // namespace drmpp::input
