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

#ifndef INCLUDE_DRMPP_INPUT_SEAT_H_
#define INCLUDE_DRMPP_INPUT_SEAT_H_

#include <list>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include <fcntl.h>
#include <libinput.h>
#include <unistd.h>

#include "drmpp.h"

namespace drmpp::input {
    class Seat;

    /**
     * \brief Interface for observing seat events.
     */
    class SeatObserver {
    public:
        /**
         * \brief Enum representing the capabilities of a seat.
         */
        enum SeatCapabilities {
            SEAT_CAPABILITIES_TOUCH = 1UL << 0, /**< Touch capability */
            SEAT_CAPABILITIES_SWITCH = 1UL << 1, /**< Switch capability */
            SEAT_CAPABILITIES_GESTURE = 1UL << 2, /**< Gesture capability */
            SEAT_CAPABILITIES_POINTER = 1UL << 3, /**< Pointer capability */
            SEAT_CAPABILITIES_KEYBOARD = 1UL << 4, /**< Keyboard capability */
            SEAT_CAPABILITIES_TABLET_PAD = 1UL << 5, /**< Tablet pad capability */
            SEAT_CAPABILITIES_TABLET_TOOL = 1UL << 6, /**< Tablet tool capability */
        };

        virtual ~SeatObserver() = default;

        /**
         * \brief Notify the observer of seat capabilities.
         *
         * \param seat Pointer to the Seat instance.
         * \param caps The capabilities of the seat.
         */
        virtual void notify_seat_capabilities(Seat *seat, uint32_t caps) = 0;
    };

    /**
     * \brief Class representing a seat.
     */
    class Seat {
    public:
        /**
         * \brief Struct representing the event mask.
         */
        struct event_mask {
            Keyboard::event_mask keyboard; /**< Keyboard event mask */
            Pointer::event_mask pointer; /**< Pointer event mask */
#if 0
            Touch::event_mask touch; /**< Touch event mask */
#endif
        };

        /**
         * \brief Constructs a Seat instance.
         *
         * \param disable_cursor Whether to disable the cursor.
         * \param ignore_events The events to be ignored.
         * \param seat_id The ID of the seat (default is "seat0").
         */
        explicit Seat(bool disable_cursor,
                      const char *ignore_events,
                      const char *seat_id = "seat0");

        /**
         * \brief Destroys the Seat instance.
         */
        ~Seat();

        /**
         * \brief Registers an observer for seat events.
         *
         * \param observer Pointer to the observer.
         * \param user_data Optional user data to be passed to the observer.
         */
        void register_observer(SeatObserver *observer, void *user_data = nullptr);

        /**
         * \brief Unregisters an observer for seat events.
         *
         * \param observer Pointer to the observer.
         */
        void unregister_observer(SeatObserver *observer);

        /**
         * \brief Runs the seat event loop once.
         *
         * \return True if an event was handled, false otherwise.
         */
        bool run_once();

        /**
         * \brief Gets the user data.
         *
         * \return Pointer to the user data.
         */
        [[nodiscard]] void *get_user_data() const { return user_data_; }

        //[[nodiscard]] wl_seat *get_seat() const { return wl_seat_; }

        /**
         * \brief Gets the capabilities of the seat.
         *
         * \return The capabilities of the seat.
         */
        [[nodiscard]] uint32_t get_capabilities() const { return capabilities_; }

        /**
         * \brief Gets the name of the seat.
         *
         * \return The name of the seat.
         */
        [[nodiscard]] const std::string &get_name() const { return name_; }

        /**
         * \brief Gets the keyboards associated with the seat.
         *
         * \return An optional shared pointer to a vector of unique pointers to
         * Keyboard instances.
         */
        [[nodiscard]] std::optional<
            std::shared_ptr<std::vector<std::unique_ptr<Keyboard> > > >
        get_keyboards() const;

        /**
         * \brief Gets the pointer associated with the seat.
         *
         * \return An optional shared pointer to a Pointer instance.
         */
        [[nodiscard]] std::optional<std::shared_ptr<Pointer> > get_pointer() const;

        /**
         * \brief Sets the event mask.
         *
         * \param ignore_events The events to be ignored.
         */
        void set_event_mask(const char *ignore_events);

        // Disallow copy and assign.
        Seat(const Seat &) = delete;

        Seat &operator=(const Seat &) = delete;

    private:
        libinput *li_{}; /**< libinput context */
        udev *udev_{}; /**< udev context */

        uint32_t capabilities_{}; /**< Capabilities of the seat */
        bool capabilities_init_ = true; /**< Whether capabilities are initialized */
        std::string name_; /**< Name of the seat */
        bool disable_cursor_; /**< Whether the cursor is disabled */
        void *user_data_{}; /**< User data */
        event_mask event_mask_{}; /**< Event mask */

        std::list<SeatObserver *> observers_{}; /**< List of observers */
        std::mutex observers_mutex_{}; /**< Mutex for observers list */

        std::shared_ptr<std::vector<std::unique_ptr<Keyboard> > >
        keyboards_; /**< Keyboards associated with the seat */
        std::shared_ptr<Pointer> pointer_; /**< Pointer associated with the seat */
        // std::unique_ptr<Touch> touch_;

        /**
         * \brief Handles seat capabilities.
         *
         * \param data Pointer to the user data.
         * \param caps The capabilities of the seat.
         */
        static void handle_capabilities(void *data, uint32_t caps);

        /**
         * \brief Handles seat name.
         *
         * \param data Pointer to the user data.
         * \param name The name of the seat.
         */
        static void handle_name(void *data, const char *name);

        /**
         * \brief Prints the event mask.
         */
        static void event_mask_print();

        /**
         * \brief libinput interface for opening and closing restricted files.
         */
        static constexpr libinput_interface interface_ = {
            .open_restricted =
            [](const char *path, const int flags, void * /* user_data */) {
                const int fd = open(path, flags);
                return fd < 0 ? -errno : fd;
            },
            .close_restricted = [](const int fd,
                                   void * /* user_data */) {
                close(fd);
            },
        };
    };
} // namespace drmpp::input

#endif  // INCLUDE_DRMPP_INPUT_SEAT_H_
