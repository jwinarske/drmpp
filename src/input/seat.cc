
#include "input/seat.h"

#include <algorithm>
#include <sstream>

#include "linux/input-event-codes.h"

#include "drmpp.h"
#include "input/keyboard.h"
#include "input/pointer.h"

namespace drmpp::input {
  /**
   * @class Seat
   * @brief Represents a seat in a Wayland compositor.
   *
   * The Seat class provides a representation of a seat in a Wayland compositor.
   * It is used to handle input events from devices such as keyboards, pointers,
   * and touchscreens.
   */
  Seat::Seat(const bool disable_cursor,
             const char *ignore_events,
             const char *seat_id)
    : udev_(udev_new()), disable_cursor_(disable_cursor) {
    li_ = libinput_udev_create_context(&interface_, nullptr, udev_);
    libinput_log_set_priority(li_, LIBINPUT_LOG_PRIORITY_INFO);
    libinput_log_set_handler(li_, [](libinput * /* libinput */, const libinput_log_priority priority,
                                     const char *format,
                                     va_list args) {
      std::vector<char> buff(1024);
      if (vsnprintf(buff.data(), 1024, format, args) < 0) {
        LOG_ERROR("libinput log exceeded 1024 in length");
        return;
      }
      std::string result(&buff[0]);
      switch (priority) {
        case LIBINPUT_LOG_PRIORITY_DEBUG:
          LOG_DEBUG("[libinput] {}", utils::trim(result, "\n"));
          break;
        case LIBINPUT_LOG_PRIORITY_INFO:
          LOG_INFO("[libinput] {}", utils::trim(result, "\n"));
          break;
        case LIBINPUT_LOG_PRIORITY_ERROR:
          LOG_ERROR("[libinput] {}", utils::trim(result, "\n"));
          break;
        default:
          break;
      }
    });

    libinput_udev_assign_seat(li_, seat_id);

    if (ignore_events) {
      set_event_mask(ignore_events);
    }
  }

  Seat::~Seat() {
    if (keyboards_ && !keyboards_->empty()) {
      for (auto &keyboard: *keyboards_) {
        keyboard.reset();
      }
    }

    if (li_) {
      libinput_unref(li_);
    }
    if (udev_) {
      udev_unref(udev_);
    }
  }

  void Seat::register_observer(SeatObserver *observer, void *user_data) {
    std::scoped_lock<std::mutex> lock(observers_mutex_);
    observers_.push_back(observer);

    if (user_data) {
      user_data_ = user_data;
    }
  }

  void Seat::unregister_observer(SeatObserver *observer) {
    std::scoped_lock<std::mutex> lock(observers_mutex_);
    observers_.remove(observer);
  }

  bool Seat::run_once() {
    libinput_dispatch(li_);

    const auto ev = libinput_get_event(li_);
    if (ev) {
      auto type = libinput_event_get_type(ev);
      DLOG_TRACE("Event: {}", static_cast<int>(type));

      if (capabilities_init_ && type != LIBINPUT_EVENT_DEVICE_ADDED) {
        capabilities_init_ = false;
        std::scoped_lock<std::mutex> lock(observers_mutex_);
        for (const auto observer: observers_) {
          observer->notify_seat_capabilities(this, capabilities_);
        }
      }
      const auto dev = libinput_event_get_device(ev);

      switch (type) {
        case LIBINPUT_EVENT_DEVICE_ADDED: {
          const auto name = libinput_device_get_name(dev);

          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH)) {
            capabilities_ |= SeatObserver::SEAT_CAPABILITIES_TOUCH;
            DLOG_TRACE("Added Touch: {}", name);
          }
          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_SWITCH)) {
            capabilities_ |= SeatObserver::SEAT_CAPABILITIES_SWITCH;
            DLOG_TRACE("Added Switch: {}", name);
          }
          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_GESTURE)) {
            capabilities_ |= SeatObserver::SEAT_CAPABILITIES_GESTURE;
            DLOG_TRACE("Added Gesture: {}", name);
          }
          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER)) {
            if (libinput_device_pointer_has_button(dev, BTN_LEFT)) {
              if (!pointer_) {
                pointer_ = std::make_shared<Pointer>(disable_cursor_, event_mask_.pointer);
              }
              capabilities_ |= SeatObserver::SEAT_CAPABILITIES_POINTER;
              DLOG_TRACE("Added Pointer: {}", name);
            }
          }
          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
            if (libinput_device_keyboard_has_key(dev, KEY_ENTER) ||
                libinput_device_keyboard_has_key(dev, KEY_KPENTER)) {
              const auto udev_device = libinput_device_get_udev_device(dev);
              if (!keyboards_) {
                keyboards_ = std::make_shared<std::vector<std::unique_ptr<Keyboard> > >();
              }
              keyboards_->emplace_back(std::make_unique<Keyboard>(
                event_mask_.keyboard,
                udev_device_get_property_value(udev_device, "XKBMODEL"),
                udev_device_get_property_value(udev_device, "XKBLAYOUT"),
                udev_device_get_property_value(udev_device, "XKBVARIANT"),
                udev_device_get_property_value(udev_device, "XKBOPTIONS")));
              udev_device_unref(udev_device);
              capabilities_ |= SeatObserver::SEAT_CAPABILITIES_KEYBOARD;
              DLOG_TRACE("Added Keyboard: {}", name);
            }
          }
          if (libinput_device_has_capability(dev,
                                             LIBINPUT_DEVICE_CAP_TABLET_PAD)) {
            capabilities_ |= SeatObserver::SEAT_CAPABILITIES_TABLET_PAD;
            DLOG_TRACE("Added Tablet Pad: {}", name);
          }
          if (libinput_device_has_capability(dev,
                                             LIBINPUT_DEVICE_CAP_TABLET_TOOL)) {
            capabilities_ |= SeatObserver::SEAT_CAPABILITIES_TABLET_TOOL;
            DLOG_TRACE("Added Tablet Tool: {}", name);
          }
          break;
        }
        case LIBINPUT_EVENT_DEVICE_REMOVED: {
          libinput_device *dev = libinput_event_get_device(ev);
          const auto name = libinput_device_get_name(dev);

          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH)) {
            DLOG_TRACE("{}: Touch Removed", name);
          }
          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_SWITCH)) {
            DLOG_TRACE("{}: Switch Removed", name);
          }
          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_GESTURE)) {
            DLOG_TRACE("{}: Gesture Removed", name);
          }
          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER)) {
            DLOG_TRACE("{}: Pointer Removed", name);
          }
          if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
            DLOG_TRACE("{}: Keyboard Removed", name);
          }
          if (libinput_device_has_capability(dev,
                                             LIBINPUT_DEVICE_CAP_TABLET_PAD)) {
            DLOG_TRACE("{}: Tablet Pad Removed", name);
          }
          if (libinput_device_has_capability(dev,
                                             LIBINPUT_DEVICE_CAP_TABLET_TOOL)) {
            DLOG_TRACE("{}: Tablet Tool Removed", name);
          }
          break;
        }
        case LIBINPUT_EVENT_KEYBOARD_KEY: {
          const auto key_event = libinput_event_get_keyboard_event(ev);
          for (const auto &keyboard: *keyboards_) {
            keyboard->handle_keyboard_event(key_event);
          }
          break;
        }
        case LIBINPUT_EVENT_POINTER_BUTTON:
          pointer_->handle_pointer_button_event(libinput_event_get_pointer_event(ev));
          break;
        case LIBINPUT_EVENT_POINTER_MOTION:
          pointer_->handle_pointer_motion_event(libinput_event_get_pointer_event(ev));
          break;
        case LIBINPUT_EVENT_POINTER_AXIS:
          pointer_->handle_pointer_axis_event(libinput_event_get_pointer_event(ev));
          break;
        case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
          pointer_->handle_pointer_motion_absolute_event(libinput_event_get_pointer_event(ev));
          break;
        }
        default: {
          LOG_INFO("Event Type: {}", static_cast<int>(type));
          break;
        }
      }
      libinput_event_destroy(ev);
    }

    return true;
  }

  std::optional<std::shared_ptr<std::vector<std::unique_ptr<Keyboard> > > > Seat::get_keyboards() const {
    if (keyboards_) {
      return keyboards_;
    }
    return {};
  }

  std::optional<std::shared_ptr<Pointer> > Seat::get_pointer() const {
    if (!pointer_) {
      return {};
    }
    return pointer_;
  }

  void Seat::event_mask_print() const {
    const std::string out;
    std::stringstream ss(out);
    ss << "Seat Event Mask";

#if 0   // TODO
    if (event_mask_.pointer.enabled)
        ss << "\n\tpointer [enabled]";
    if (event_mask_.pointer.all)
        ss << "\n\tpointer";
    if (event_mask_.pointer.axis)
        ss << "\n\tpointer-axis";
    if (event_mask_.pointer.buttons)
        ss << "\n\tpointer-buttons";
    if (event_mask_.pointer.motion)
        ss << "\n\tpointer-motion";
    if (event_mask_.keyboard.enabled)
        ss << "\n\tkeyboard [enabled]";
    if (event_mask_.keyboard.all)
        ss << "\n\tkeyboard";
    if (event_mask_.touch.enabled)
        ss << "\n\ttouch [enabled]";
    if (event_mask_.touch.all)
        ss << "\n\ttouch";
#endif  // TODO
    LOG_INFO(ss.str());
  }

  void Seat::set_event_mask(const char *ignore_events) {
    std::string mask_events(ignore_events);
    if (mask_events.empty()) {
      return;
    }

    std::string events;
    events.reserve(mask_events.size());
    for (const char event: mask_events) {
      if (event != ' ' && event != '"')
        events += event;
    }

    std::transform(
      events.begin(), events.end(), events.begin(),
      [](const char c) { return std::tolower(static_cast<unsigned char>(c)); });

    std::stringstream ss(events);
    while (ss.good()) {
      std::string event;
      getline(ss, event, ',');
#if 0
            if (event.rfind("pointer", 0) == 0) {
                event_mask_.pointer.enabled = true;
                if (event == "pointer-axis") {
                    event_mask_.pointer.axis = true;
                } else if (event == "pointer-buttons") {
                    event_mask_.pointer.buttons = true;
                } else if (event == "pointer-motion") {
                    event_mask_.pointer.motion = true;
                } else if (event == "pointer") {
                    event_mask_.pointer.all = true;
                }
                if (pointer_) {
                    pointer_->set_event_mask(event_mask_.pointer);
                }
            } else
#endif
      if (event.rfind("keyboard", 0) == 0) {
        event_mask_.keyboard.enabled = true;
        if (event == "keyboard") {
          event_mask_.keyboard.all = true;
        }
        for (const auto &keyboard: *keyboards_) {
          keyboard->set_event_mask(event_mask_.keyboard);
        }
      }
#if 0
            else if (event.rfind("touch", 0) == 0) {
                event_mask_.touch.all = true;
                if (event == "touch") {
                    event_mask_.touch.enabled = true;
                }
                if (touch_) {
                    touch_->set_event_mask(event_mask_.touch);
                }
            }
#endif
      else {
        LOG_WARN("Unknown Event Mask: [{}]", event);
      }
    }
    if (!mask_events.empty()) {
      event_mask_print();
    }
  }
} // namespace drmpp::input
