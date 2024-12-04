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

#ifndef INCLUDE_DRMPP_INPUT_KEYBOARD_H_
#define INCLUDE_DRMPP_INPUT_KEYBOARD_H_

#include <list>
#include <mutex>
#include <string>
#include <utility>

#include <libinput.h>
#include <xkbcommon/xkbcommon.h>
#include <csignal>

namespace drmpp::input {
class Keyboard;

/**
 * \brief Interface for observing keyboard events.
 */
class KeyboardObserver {
 public:
  /**
   * \brief Enum representing the state of a key.
   */
  enum KeyState {
    KEY_STATE_RELEASE, /**< Key is released */
    KEY_STATE_PRESS,   /**< Key is pressed */
  };

  virtual ~KeyboardObserver() = default;

  /**
   * \brief Notify the observer of a key event.
   *
   * \param keyboard Pointer to the keyboard instance.
   * \param time The time of the event.
   * \param xkb_scancode The XKB scancode of the key.
   * \param keymap_key_repeats Whether the keymap key repeats.
   * \param state The state of the key (pressed or released).
   * \param xdg_key_symbol_count The number of key symbols.
   * \param xdg_key_symbols Pointer to the key symbols.
   */
  virtual void notify_keyboard_xkb_v1_key(
      Keyboard* keyboard,
      uint32_t time,
      uint32_t xkb_scancode,
      bool keymap_key_repeats,
      uint32_t state,
      int xdg_key_symbol_count,
      const xkb_keysym_t* xdg_key_symbols) = 0;
};

/**
 * \brief Class representing a keyboard.
 */
class Keyboard {
 public:
  /**
   * \brief Struct representing the event mask.
   */
  struct event_mask {
    bool enabled; /**< Whether the event mask is enabled */
    bool all;     /**< Whether all events are masked */
  };

  /**
   * \brief Constructs a Keyboard instance.
   *
   * \param event_mask The event mask to be used.
   * \param model The XKB model.
   * \param layout The XKB layout.
   * \param variant The XKB variant.
   * \param options The XKB options.
   * \param delay The delay for key repeat (default is 500).
   * \param repeat The repeat rate for key repeat (default is 33).
   */
  explicit Keyboard(event_mask const& event_mask,
                    const char* model,
                    const char* layout,
                    const char* variant,
                    const char* options,
                    int32_t delay = 500,
                    int32_t repeat = 33);

  /**
   * \brief Destroys the Keyboard instance.
   */
  ~Keyboard();

  /**
   * \brief Registers an observer for keyboard events.
   *
   * \param observer Pointer to the observer.
   * \param user_data Optional user data to be passed to the observer.
   */
  void register_observer(KeyboardObserver* observer, void* user_data = nullptr);

  /**
   * \brief Unregisters an observer for keyboard events.
   *
   * \param observer Pointer to the observer.
   */
  void unregister_observer(KeyboardObserver* observer);

  /**
   * \brief Sets the user data.
   *
   * \param user_data Pointer to the user data.
   */
  void set_user_data(void* user_data) { user_data_ = user_data; }

  /**
   * \brief Gets the user data.
   *
   * \return Pointer to the user data.
   */
  [[nodiscard]] void* get_user_data() const { return user_data_; }

  /**
   * \brief Handles a keyboard event.
   *
   * \param key_event Pointer to the keyboard event.
   */
  void handle_keyboard_event(libinput_event_keyboard* key_event);

  /**
   * \brief Sets the event mask.
   *
   * \param event_mask The event mask to be set.
   */
  void set_event_mask(event_mask const& event_mask);

  // Disallow copy and assign.
  Keyboard(const Keyboard&) = delete;

  Keyboard& operator=(const Keyboard&) = delete;

 private:
  std::list<KeyboardObserver*> observers_{}; /**< List of observers */
  std::mutex observers_mutex_{};             /**< Mutex for observers list */
  void* user_data_{};                        /**< User data */
  event_mask event_mask_{};                  /**< Event mask */

  xkb_context* xkb_context_{}; /**< XKB context */
  xkb_keymap* xkb_keymap_{};   /**< XKB keymap */
  xkb_state* xkb_state_{};     /**< XKB state */

  struct {
    int32_t rate;        /**< Repeat rate */
    int32_t delay;       /**< Repeat delay */
    timer_t timer;       /**< Timer for key repeat */
    uint32_t code;       /**< Key code */
    sigevent sev;        /**< Signal event */
    struct sigaction sa; /**< Signal action */

    struct {
      uint32_t serial;              /**< Serial number */
      uint32_t time;                /**< Time of the event */
      uint32_t xkb_scancode;        /**< XKB scancode */
      int key_repeats;              /**< Key repeats */
      int xdg_keysym_count;         /**< Key symbol count */
      const xkb_keysym_t* key_syms; /**< Key symbols */
    } notify;                       /**< Notification data */
  } repeat_{};                      /**< Repeat data */

  /**
   * \brief Loads the default keymap.
   */
  void load_default_keymap();

  /**
   * \brief Loads the keymap from a file.
   *
   * \param keymap_file The path to the keymap file.
   */
  void load_keymap_from_file(const std::string& keymap_file);

  /**
   * \brief Logs XKB messages.
   *
   * \param context Pointer to the XKB context.
   * \param level The log level.
   * \param format The log message format.
   * \param args The log message arguments.
   */
  static void xkb_log(xkb_context* context,
                      xkb_log_level level,
                      const char* format,
                      va_list args);

  /**
   * \brief Handles repeat information.
   *
   * \param delay The repeat delay.
   * \param rate The repeat rate.
   */
  void handle_repeat_info(int32_t delay, int32_t rate);

  /**
   * \brief Callback for key repeat events.
   *
   * \param sig The signal number.
   * \param si Pointer to the signal information.
   * \param uc Pointer to the user context.
   */
  static void repeat_xkb_v1_key_callback(int sig, siginfo_t* si, void* uc);

  /**
   * \brief Gets the file path for the keymap.
   *
   * \return A pair containing the keymap file path and the keymap file name.
   */
  static std::pair<std::string, std::string> get_keymap_filepath();
};
}  // namespace drmpp::input

#endif  // INCLUDE_DRMPP_INPUT_KEYBOARD_H_
