
#include "input/seat.h"

#include <sstream>

#include "input/keyboard.h"
#include "drmpp.h"

namespace drmpp::input {
    /**
     * @class Seat
     * @brief Represents a seat in a Wayland compositor.
     *
     * The Seat class provides a representation of a seat in a Wayland compositor.
     * It is used to handle input events from devices such as keyboards, pointers,
     * and touchscreens.
     */
    Seat::Seat(bool disable_cursor,
               const char *ignore_events)
        : udev_(udev_new()), disable_cursor_(disable_cursor) {
        li_ = libinput_udev_create_context(&interface_, nullptr, udev_);
        libinput_udev_assign_seat(li_, "seat0");

        if (ignore_events) {
            set_event_mask(ignore_events);
        }
    }

    Seat::~Seat() {
        if (li_) {
            libinput_unref(li_);
        }
        if (udev_) {
            udev_unref(udev_);
        }
    }

    bool Seat::run_once() {
        libinput_dispatch(li_);

        const auto ev = libinput_get_event(li_);
        if (ev) {
            libinput_device *dev = libinput_event_get_device(ev);
            const char *name = libinput_device_get_name(dev);

            auto type = libinput_event_get_type(ev);
            switch (type) {
                case LIBINPUT_EVENT_DEVICE_ADDED: {
                    if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH)) {
                        LOG_INFO("{}: Touch Added", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_SWITCH)) {
                        LOG_INFO("{}: Switch Added", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_GESTURE)) {
                        LOG_INFO("{}: Gesture Added", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER)) {
                        LOG_INFO("{}: Pointer Added", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
                        keyboard_ = std::make_unique<drmpp::input::Keyboard>();
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TABLET_PAD)) {
                        LOG_INFO("{}: Tablet Pad Added", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TABLET_TOOL)) {
                        LOG_INFO("{}: Tablet Tool Added", name);
                    }
                    break;
                }
                case LIBINPUT_EVENT_DEVICE_REMOVED: {
                    if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TOUCH)) {
                        LOG_INFO("{}: Touch Removed", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_SWITCH)) {
                        LOG_INFO("{}: Switch Removed", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_GESTURE)) {
                        LOG_INFO("{}: Gesture Removed", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_POINTER)) {
                        LOG_INFO("{}: Pointer Removed", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_KEYBOARD)) {
                        keyboard_.reset();
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TABLET_PAD)) {
                        LOG_INFO("{}: Tablet Pad Removed", name);
                    } else if (libinput_device_has_capability(dev, LIBINPUT_DEVICE_CAP_TABLET_TOOL)) {
                        LOG_INFO("{}: Tablet Tool Removed", name);
                    }
                    break;
                }
                case LIBINPUT_EVENT_KEYBOARD_KEY: {
                    const auto key_event = libinput_event_get_keyboard_event(ev);
                    keyboard_->handle_key(key_event);
                    break;
                }
                case LIBINPUT_EVENT_POINTER_BUTTON:
                case LIBINPUT_EVENT_POINTER_MOTION:
                case LIBINPUT_EVENT_POINTER_AXIS:
                case LIBINPUT_EVENT_POINTER_MOTION_ABSOLUTE: {
                    // const auto pointer_event = libinput_event_get_pointer_event(ev);
                    //TODO handle_pointer_event(type, pointer_event);
                    break;
                }
                default: {
                    LOG_INFO("Event Type: {}", static_cast<int>(type));
                    break;
                }
            }
        }
        libinput_event_destroy(ev);

        return true;
    }

    std::optional<Keyboard *> Seat::get_keyboard() const {
        if (keyboard_) {
            return keyboard_.get();
        }
        return {};
    }

#if 0
    std::optional<Pointer *> Seat::get_pointer() const {
        if (pointer_) {
            return pointer_.get();
        }
        return {};
    }
#endif

    void Seat::event_mask_print() const {
        const std::string out;
        std::stringstream ss(out);
        ss << "Seat Event Mask";

#if 0 //TODO
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
#endif //TODO
        LOG_INFO(ss.str());
    }

    void Seat::set_event_mask(const char *ignore_events) {
        std::string ignore_wayland_events(ignore_events);

        std::string events;
        events.reserve(ignore_wayland_events.size());
        for (const char event: ignore_wayland_events) {
            if (event != ' ' && event != '"')
                events += event;
        }

        std::transform(
            events.begin(), events.end(), events.begin(),
            [](const char c) { return std::tolower(static_cast<unsigned char>(c)); });

#if 0
    std::stringstream ss(events);
    while (ss.good()) {
        std::string event;
        getline(ss, event, ',');
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
        } else if (event.rfind("keyboard", 0) == 0) {
            event_mask_.keyboard.enabled = true;
            if (event == "keyboard") {
                event_mask_.keyboard.all = true;
            }
            if (keyboard_) {
                keyboard_->set_event_mask(event_mask_.keyboard);
            }
        } else if (event.rfind("touch", 0) == 0) {
            event_mask_.touch.all = true;
            if (event == "touch") {
                event_mask_.touch.enabled = true;
            }
            if (touch_) {
                touch_->set_event_mask(event_mask_.touch);
            }
        } else {
            LOG_WARN("Unknown Event Mask: [{}]", event);
        }
    }
#endif
        if (!ignore_wayland_events.empty()) {
            event_mask_print();
        }
    }
}
