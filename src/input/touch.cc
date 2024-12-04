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

#include "input/touch.h"

namespace drmpp::input {
Touch::Touch(event_mask const& event_mask) {
  event_mask_ = {
      .enabled = event_mask.enabled,
      .all = event_mask.all,
  };
}

Touch::~Touch() = default;

void Touch::set_event_mask(event_mask const& event_mask) {
  event_mask_.enabled = event_mask.enabled;
  event_mask_.all = event_mask.all;
}

void Touch::handle_touch_up(libinput_event_touch* ev) {
  std::scoped_lock lock(observers_mutex_);
  for (const auto observer : observers_) {
    observer->notify_touch_up(this, libinput_event_touch_get_time(ev),
                              libinput_event_touch_get_x(ev),
                              libinput_event_touch_get_y(ev));
  }
}

void Touch::handle_touch_down(libinput_event_touch* ev) {
  std::scoped_lock lock(observers_mutex_);
  for (const auto observer : observers_) {
    observer->notify_touch_down(this, libinput_event_touch_get_time(ev),
                                libinput_event_touch_get_x(ev),
                                libinput_event_touch_get_y(ev));
  }
}

void Touch::handle_touch_frame(libinput_event_touch* ev) {
  std::scoped_lock lock(observers_mutex_);
  for (const auto observer : observers_) {
    observer->notify_touch_frame(this, libinput_event_touch_get_time(ev));
  }
}

void Touch::handle_touch_cancel(libinput_event_touch* ev) {
  std::scoped_lock lock(observers_mutex_);
  for (const auto observer : observers_) {
    observer->notify_touch_cancel(this, libinput_event_touch_get_time(ev));
  }
}

void Touch::handle_touch_motion(libinput_event_touch* ev) {
  std::scoped_lock lock(observers_mutex_);
  for (const auto observer : observers_) {
    observer->notify_touch_motion(this, libinput_event_touch_get_time(ev),
                                  libinput_event_touch_get_x(ev),
                                  libinput_event_touch_get_y(ev));
  }
}

}  // namespace drmpp::input