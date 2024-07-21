#ifndef INCLUDE_DRMPP_INPUT_SEAT_H_
#define INCLUDE_DRMPP_INPUT_SEAT_H_

#include <optional>
#include <list>
#include <memory>
#include <string>

#include <csignal>
#include <fcntl.h>
#include <libinput.h>
#include <unistd.h>
#include <xkbcommon/xkbcommon.h>

#include "input/keyboard.h"

namespace drmpp::input {

    class Touch;

    class Seat;

    class SeatObserver {
    public:
        virtual ~SeatObserver() = default;

        virtual void notify_seat_name(Seat *seat,
                                      const char *name) = 0;

        virtual void notify_seat_capabilities(Seat *seat,
                                              uint32_t caps) = 0;
    };

    class Seat {
    public:
        struct event_mask {
#if 0
            Pointer::event_mask pointer;
            Keyboard::event_mask keyboard;
            Touch::event_mask touch;
#endif
        };

        explicit Seat(bool disable_cursor,
                      const char *ignore_events);

        ~Seat();

        void register_observer(SeatObserver *observer, void *user_data = nullptr) {
            observers_.push_back(observer);

            if (user_data) {
                user_data_ = user_data;
            }
        }

        void unregister_observer(SeatObserver *observer) {
            observers_.remove(observer);
        }

        bool run_once();

        [[nodiscard]] void *get_user_data() const { return user_data_; }

        //        [[nodiscard]] wl_seat *get_seat() const { return wl_seat_; }

        [[nodiscard]] uint32_t get_capabilities() const { return capabilities_; }

        [[nodiscard]] const std::string &get_name() const { return name_; }

        [[nodiscard]] std::optional<Keyboard *> get_keyboard() const;

        //        [[nodiscard]] std::optional<Pointer *> get_pointer() const;

        void set_event_mask(const char *ignore_events);

        // Disallow copy and assign.
        Seat(const Seat &) = delete;

        Seat &operator=(const Seat &) = delete;

    private:
        libinput *li_{};
        udev *udev_{};

        uint32_t capabilities_{};
        std::string name_;
        struct wl_shm *wl_shm_;
        struct wl_compositor *wl_compositor_;
        bool disable_cursor_;
        void *user_data_{};
        event_mask event_mask_{};

        std::list<SeatObserver *> observers_{};

        std::unique_ptr<Keyboard> keyboard_;
        //std::unique_ptr<Pointer> pointer_;
        //std::unique_ptr<Touch> touch_;

        static void handle_capabilities(void *data,
                                        uint32_t caps);

        static void handle_name(void *data,
                                const char *name);

        void event_mask_print() const;

        static constexpr libinput_interface interface_ = {
            .open_restricted = [](const char *path, const int flags, void * /* user_data */) {
                const int fd = open(path, flags);
                return fd < 0 ? -errno : fd;
            },
            .close_restricted = [](const int fd, void * /* user_data */) {
                close(fd);
            },
        };
    };
}

#endif // INCLUDE_DRMPP_INPUT_SEAT_H_
