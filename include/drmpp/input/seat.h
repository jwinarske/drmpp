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

class SeatObserver {
 public:
  enum SeatCapabilities {
    SEAT_CAPABILITIES_TOUCH = 1UL << 0,
    SEAT_CAPABILITIES_SWITCH = 1UL << 1,
    SEAT_CAPABILITIES_GESTURE = 1UL << 2,
    SEAT_CAPABILITIES_POINTER = 1UL << 3,
    SEAT_CAPABILITIES_KEYBOARD = 1UL << 4,
    SEAT_CAPABILITIES_TABLET_PAD = 1UL << 5,
    SEAT_CAPABILITIES_TABLET_TOOL = 1UL << 6,
  };

  virtual ~SeatObserver() = default;

  virtual void notify_seat_capabilities(Seat* seat, uint32_t caps) = 0;
};

class Seat {
 public:
  struct event_mask {
    Keyboard::event_mask keyboard;
    Pointer::event_mask pointer;
#if 0
            Touch::event_mask touch;
#endif
  };

  explicit Seat(bool disable_cursor,
                const char* ignore_events,
                const char* seat_id = "seat0");

  ~Seat();

  void register_observer(SeatObserver* observer, void* user_data = nullptr);

  void unregister_observer(SeatObserver* observer);

  bool run_once();

  [[nodiscard]] void* get_user_data() const { return user_data_; }

  //[[nodiscard]] wl_seat *get_seat() const { return wl_seat_; }

  [[nodiscard]] uint32_t get_capabilities() const { return capabilities_; }

  [[nodiscard]] const std::string& get_name() const { return name_; }

  [[nodiscard]] std::optional<
      std::shared_ptr<std::vector<std::unique_ptr<Keyboard>>>>
  get_keyboards() const;

  [[nodiscard]] std::optional<std::shared_ptr<Pointer>> get_pointer() const;

  void set_event_mask(const char* ignore_events);

  // Disallow copy and assign.
  Seat(const Seat&) = delete;

  Seat& operator=(const Seat&) = delete;

 private:
  libinput* li_{};
  udev* udev_{};

  uint32_t capabilities_{};
  bool capabilities_init_ = true;
  std::string name_;
  bool disable_cursor_;
  void* user_data_{};
  event_mask event_mask_{};

  std::list<SeatObserver*> observers_{};
  std::mutex observers_mutex_{};

  std::shared_ptr<std::vector<std::unique_ptr<Keyboard>>> keyboards_;
  std::shared_ptr<Pointer> pointer_;
  // std::unique_ptr<Touch> touch_;

  static void handle_capabilities(void* data, uint32_t caps);

  static void handle_name(void* data, const char* name);

  static void event_mask_print();

  static constexpr libinput_interface interface_ = {
      .open_restricted =
          [](const char* path, const int flags, void* /* user_data */) {
            const int fd = open(path, flags);
            return fd < 0 ? -errno : fd;
          },
      .close_restricted = [](const int fd,
                             void* /* user_data */) { close(fd); },
  };
};
}  // namespace drmpp::input

#endif  // INCLUDE_DRMPP_INPUT_SEAT_H_
