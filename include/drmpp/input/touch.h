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

#ifndef INCLUDE_DRMPP_INPUT_TOUCH_H_
#define INCLUDE_DRMPP_INPUT_TOUCH_H_

#include <list>
#include <mutex>

#include <libinput.h>

namespace drmpp::input {
class Touch;

/**
 * \brief Interface for observing touch events.
 */
class TouchObserver {
 public:
  /**
   * \brief Virtual destructor for the TouchObserver class.
   */
  virtual ~TouchObserver() = default;

  /**
   * \brief Notify the observer of a touch up event.
   *
   * \param touch Pointer to the Touch object.
   * \param time Event time.
   * \param x X coordinate of the touch.
   * \param y Y coordinate of the touch.
   */
  virtual void notify_touch_up(Touch* touch,
                               uint32_t time,
                               double x,
                               double y) = 0;

  /**
   * \brief Notify the observer of a touch down event.
   *
   * \param touch Pointer to the Touch object.
   * \param time Event time.
   * \param x X coordinate of the touch.
   * \param y Y coordinate of the touch.
   */
  virtual void notify_touch_down(Touch* touch,
                                 uint32_t time,
                                 double x,
                                 double y) = 0;

  /**
   * \brief Notify the observer of a touch frame event.
   *
   * \param touch Pointer to the Touch object.
   * \param time Event time.
   */
  virtual void notify_touch_frame(Touch* touch, uint32_t time);

  /**
   * \brief Notify the observer of a touch cancel event.
   *
   * \param touch Pointer to the Touch object.
   * \param time Event time.
   */
  virtual void notify_touch_cancel(Touch* touch, uint32_t time);

  /**
   * \brief Notify the observer of a touch motion event.
   *
   * \param touch Pointer to the Touch object.
   * \param time Event time.
   * \param sx X coordinate of the touch motion.
   * \param sy Y coordinate of the touch motion.
   */
  virtual void notify_touch_motion(Touch* touch,
                                   uint32_t time,
                                   double sx,
                                   double sy) = 0;
};

/**
 * \brief Class representing a touch input device.
 */
class Touch {
 public:
  /**
   * \brief Struct representing the event mask.
   */
  struct event_mask {
    bool enabled; /**< Whether the event mask is enabled */
    bool all;     /**< Whether all events are masked */
  };

  /**
   * \brief Default constructor for the Touch class.
   *
   * \param event_mask The initial event mask.
   */
  explicit Touch(event_mask const& event_mask);

  /**
   * \brief Destructor for the Touch class.
   */
  ~Touch();

  /**
   * \brief Registers an observer for touch events.
   *
   * \param observer The observer to be registered.
   * \param user_data User data to be passed to the observer.
   */
  void register_observer(TouchObserver* observer, void* user_data = nullptr) {
    observers_.push_back(observer);

    if (user_data) {
      user_data_ = user_data;
    }
  }

  /**
   * \brief Unregisters an observer for touch events.
   *
   * \param observer The observer to be unregistered.
   */
  void unregister_observer(TouchObserver* observer) {
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
   * \brief Sets the event mask.
   *
   * \param event_mask The event mask to be set.
   */
  void set_event_mask(event_mask const& event_mask);

  /**
   * \brief Handles the touch up event.
   *
   * \param ev Pointer to the libinput event.
   */
  void handle_touch_up(libinput_event_touch* ev);

  /**
   * \brief Handles the touch down event.
   *
   * \param ev Pointer to the libinput event.
   */
  void handle_touch_down(libinput_event_touch* ev);

  /**
   * \brief Handles the touch frame event.
   *
   * \param ev Pointer to the libinput event.
   */
  void handle_touch_frame(libinput_event_touch* ev);

  /**
   * \brief Handles the touch cancel event.
   *
   * \param ev Pointer to the libinput event.
   */
  void handle_touch_cancel(libinput_event_touch* ev);

  /**
   * \brief Handles the touch motion event.
   *
   * \param ev Pointer to the libinput event.
   */
  void handle_touch_motion(libinput_event_touch* ev);

  // Disallow copy and assign.
  Touch(const Touch&) = delete;

  Touch& operator=(const Touch&) = delete;

 private:
  std::list<TouchObserver*> observers_{}; /**< List of observers */
  std::mutex observers_mutex_{};          /**< Mutex for observers list */
  void* user_data_{};                     /**< User data */

  event_mask event_mask_{}; /**< Event mask */
};
}  // namespace drmpp::input

#endif  // INCLUDE_DRMPP_INPUT_TOUCH_H_