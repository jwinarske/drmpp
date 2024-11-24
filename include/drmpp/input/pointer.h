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

#ifndef INCLUDE_DRMPP_INPUT_POINTER_H_
#define INCLUDE_DRMPP_INPUT_POINTER_H_

#include <cursor/xcursor.h>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

extern "C" {
#include <libinput.h>
}

namespace drmpp::input {
class Pointer;

/**
 * \brief Interface for observing pointer events.
 */
class PointerObserver {
 public:
  virtual ~PointerObserver() = default;

  /**
   * \brief Notify the observer of a pointer motion event.
   *
   * \param pointer Pointer to the Pointer instance.
   * \param time The time of the event.
   * \param sx The x-coordinate of the pointer.
   * \param sy The y-coordinate of the pointer.
   */
  virtual void notify_pointer_motion(Pointer* pointer,
                                     uint32_t time,
                                     double sx,
                                     double sy) = 0;

  /**
   * \brief Notify the observer of a pointer button event.
   *
   * \param pointer Pointer to the Pointer instance.
   * \param serial The serial number of the event.
   * \param time The time of the event.
   * \param button The button that was pressed or released.
   * \param state The state of the button (pressed or released).
   */
  virtual void notify_pointer_button(Pointer* pointer,
                                     uint32_t serial,
                                     uint32_t time,
                                     uint32_t button,
                                     uint32_t state) = 0;

  /**
   * \brief Notify the observer of a pointer axis event.
   *
   * \param pointer Pointer to the Pointer instance.
   * \param time The time of the event.
   * \param axis The axis of the event.
   * \param value The value of the axis event.
   */
  virtual void notify_pointer_axis(Pointer* pointer,
                                   uint32_t time,
                                   uint32_t axis,
                                   double value) = 0;

  /**
   * \brief Notify the observer of a pointer frame event.
   *
   * \param pointer Pointer to the Pointer instance.
   */
  virtual void notify_pointer_frame(Pointer* pointer) = 0;

  /**
   * \brief Notify the observer of a pointer axis source event.
   *
   * \param pointer Pointer to the Pointer instance.
   * \param axis_source The source of the axis event.
   */
  virtual void notify_pointer_axis_source(Pointer* pointer,
                                          uint32_t axis_source) = 0;

  /**
   * \brief Notify the observer of a pointer axis stop event.
   *
   * \param pointer Pointer to the Pointer instance.
   * \param time The time of the event.
   * \param axis The axis of the event.
   */
  virtual void notify_pointer_axis_stop(Pointer* pointer,
                                        uint32_t time,
                                        uint32_t axis) = 0;

  /**
   * \brief Notify the observer of a pointer axis discrete event.
   *
   * \param pointer Pointer to the Pointer instance.
   * \param axis The axis of the event.
   * \param discrete The discrete value of the axis event.
   */
  virtual void notify_pointer_axis_discrete(Pointer* pointer,
                                            uint32_t axis,
                                            int32_t discrete) = 0;
};

/**
 * \brief Class representing a pointer device.
 */
class Pointer {
 public:
  static constexpr int kResizeMargin = 10;

  /**
   * \brief Struct representing the event mask.
   */
  struct event_mask {
    bool enabled; /**< Whether the event mask is enabled */
    bool all;     /**< Whether all events are masked */
    bool axis;    /**< Whether axis events are masked */
    bool buttons; /**< Whether button events are masked */
    bool motion;  /**< Whether motion events are masked */
  };

  /**
   * \brief Constructs a Pointer instance.
   *
   * \param disable_cursor Whether to disable the cursor.
   * \param event_mask The event mask to be used.
   * \param size The size of the pointer (default is 24).
   */
  explicit Pointer(bool disable_cursor,
                   event_mask const& event_mask,
                   int size = 24);

  /**
   * \brief Destroys the Pointer instance.
   */
  ~Pointer();

  /**
   * \brief Registers an observer for pointer events.
   *
   * \param observer Pointer to the observer.
   * \param user_data Optional user data to be passed to the observer.
   */
  void register_observer(PointerObserver* observer, void* user_data = nullptr) {
    observers_.push_back(observer);

    if (user_data) {
      user_data_ = user_data;
    }
  }

  /**
   * \brief Unregisters an observer for pointer events.
   *
   * \param observer Pointer to the observer.
   */
  void unregister_observer(PointerObserver* observer) {
    observers_.remove(observer);
  }

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
   * \brief Gets the cursor theme.
   *
   * \return A string containing the cursor theme.
   */
  static std::string get_cursor_theme();

  /**
   * \brief Gets the available cursors for the specified theme.
   *
   * \param theme_name The name of the cursor theme (optional).
   * \return A vector of strings containing the available cursors.
   */
  static std::vector<std::string> get_available_cursors(
      const char* theme_name = nullptr);

  /**
   * \brief Sets the cursor.
   *
   * \param serial The serial number of the event.
   * \param cursor_name The name of the cursor (default is "right_ptr").
   * \param theme_name The name of the cursor theme (optional).
   */
  void set_cursor(uint32_t serial,
                  const char* cursor_name = "right_ptr",
                  const char* theme_name = nullptr) const;

  /**
   * \brief Checks if the cursor is enabled.
   *
   * \return True if the cursor is enabled, false otherwise.
   */
  [[nodiscard]] bool is_cursor_enabled() const { return !disable_cursor_; }

  /**
   * \brief Sets the event mask.
   *
   * \param event_mask The event mask to be set.
   */
  void set_event_mask(event_mask const& event_mask);

  /**
   * \brief Gets the x and y coordinates of the pointer.
   *
   * \return A pair containing the x and y coordinates of the pointer.
   */
  [[nodiscard]] std::pair<double, double> get_xy() const { return {sx_, sy_}; }

  /**
   * \brief Handles a pointer button event.
   *
   * \param ev Pointer to the libinput event.
   */
  void handle_pointer_button_event(libinput_event_pointer* ev);

  /**
   * \brief Handles a pointer motion event.
   *
   * \param ev Pointer to the libinput event.
   */
  void handle_pointer_motion_event(libinput_event_pointer* ev);

  /**
   * \brief Handles a pointer axis event.
   *
   * \param ev Pointer to the libinput event.
   */
  void handle_pointer_axis_event(libinput_event_pointer* ev);

  /**
   * \brief Handles a pointer motion absolute event.
   *
   * \param ev Pointer to the libinput event.
   */
  void handle_pointer_motion_absolute_event(libinput_event_pointer* ev);

  // Disallow copy and assign.
  Pointer(const Pointer&) = delete;

  Pointer& operator=(const Pointer&) = delete;

 private:
  /**
   * \brief Enum representing the state of a button.
   */
  enum button_state {
    POINTER_BUTTON_STATE_RELEASED, /**< Button is released */
    POINTER_BUTTON_STATE_PRESSED,  /**< Button is pressed */
  };

  /**
   * \brief Struct representing the resolution.
   */
  struct resolution_t {
    uint32_t horizontal; /**< Horizontal resolution */
    uint32_t vertical;   /**< Vertical resolution */
  };

  /**
   * \brief Struct representing a point.
   */
  struct point_t {
    int32_t x; /**< x-coordinate */
    int32_t y; /**< y-coordinate */
  };

  std::list<PointerObserver*> observers_{}; /**< List of observers */
  std::mutex observers_mutex_{};            /**< Mutex for observers list */
  bool disable_cursor_; /**< Whether the cursor is disabled */
  void* user_data_{};   /**< User data */

  std::unique_ptr<XCursor::Images> cursor_;

  double sx_{}; /**< x-coordinate of the pointer */
  double sy_{}; /**< y-coordinate of the pointer */

#if ENABLE_XDG_CLIENT
  enum xdg_toplevel_resize_edge prev_resize_edge_ =
      XDG_TOPLEVEL_RESIZE_EDGE_NONE; /**< Previous resize edge */
#endif

  event_mask event_mask_{}; /**< Event mask */
};
}  // namespace drmpp::input

#endif  // INCLUDE_DRMPP_INPUT_POINTER_H_