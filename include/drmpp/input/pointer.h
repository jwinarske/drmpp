#ifndef INCLUDE_DRMPP_INPUT_POINTER_H_
#define INCLUDE_DRMPP_INPUT_POINTER_H_

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

class PointerObserver {
 public:
  virtual ~PointerObserver() = default;

  virtual void notify_pointer_motion(Pointer* pointer,
                                     uint32_t time,
                                     double sx,
                                     double sy) = 0;

  virtual void notify_pointer_button(Pointer* pointer,
                                     uint32_t serial,
                                     uint32_t time,
                                     uint32_t button,
                                     uint32_t state) = 0;

  virtual void notify_pointer_axis(Pointer* pointer,
                                   uint32_t time,
                                   uint32_t axis,
                                   double value) = 0;

  virtual void notify_pointer_frame(Pointer* pointer) = 0;

  virtual void notify_pointer_axis_source(Pointer* pointer,
                                          uint32_t axis_source) = 0;

  virtual void notify_pointer_axis_stop(Pointer* pointer,
                                        uint32_t time,
                                        uint32_t axis) = 0;

  virtual void notify_pointer_axis_discrete(Pointer* pointer,
                                            uint32_t axis,
                                            int32_t discrete) = 0;
};

class Pointer {
 public:
  static constexpr int kResizeMargin = 10;

  struct event_mask {
    bool enabled;
    bool all;
    bool axis;
    bool buttons;
    bool motion;
  };

  explicit Pointer(bool disable_cursor,
                   event_mask const& event_mask,
                   int size = 24);

  ~Pointer();

  void register_observer(PointerObserver* observer, void* user_data = nullptr) {
    observers_.push_back(observer);

    if (user_data) {
      user_data_ = user_data;
    }
  }

  void unregister_observer(PointerObserver* observer) {
    observers_.remove(observer);
  }

  void set_user_data(void* user_data) { user_data_ = user_data; }

  [[nodiscard]] void* get_user_data() const { return user_data_; }

  static std::string get_cursor_theme();

  static std::vector<std::string> get_available_cursors(
      const char* theme_name = nullptr);

  void set_cursor(uint32_t serial,
                  const char* cursor_name = "right_ptr",
                  const char* theme_name = nullptr) const;

  [[nodiscard]] bool is_cursor_enabled() const { return !disable_cursor_; }

  void set_event_mask(event_mask const& event_mask);

  [[nodiscard]] std::pair<double, double> get_xy() const { return {sx_, sy_}; }

  void handle_pointer_button_event(libinput_event_pointer* ev);

  void handle_pointer_motion_event(libinput_event_pointer* ev);

  void handle_pointer_axis_event(libinput_event_pointer* ev);

  void handle_pointer_motion_absolute_event(libinput_event_pointer* ev);

  // Disallow copy and assign.
  Pointer(const Pointer&) = delete;

  Pointer& operator=(const Pointer&) = delete;

 private:
  enum button_state {
    POINTER_BUTTON_STATE_RELEASED,
    POINTER_BUTTON_STATE_PRESSED,
  };

  struct resolution_t {
    uint32_t horizontal;
    uint32_t vertical;
  };

  struct point_t {
    int32_t x;
    int32_t y;
  };

  std::list<PointerObserver*> observers_{};
  std::mutex observers_mutex_{};
  bool disable_cursor_;
  int size_;
  void* user_data_{};

  double sx_{};
  double sy_{};

#if ENABLE_XDG_CLIENT
  enum xdg_toplevel_resize_edge prev_resize_edge_ =
      XDG_TOPLEVEL_RESIZE_EDGE_NONE;
#endif

  event_mask event_mask_{};
};
}  // namespace drmpp::input

#endif  // INCLUDE_DRMPP_INPUT_POINTER_H_
