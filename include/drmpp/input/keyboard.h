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

  class KeyboardObserver {
  public:
    enum KeyState {
      KEY_STATE_RELEASE,
      KEY_STATE_PRESS,
    };

    virtual ~KeyboardObserver() = default;

    virtual void notify_keyboard_xkb_v1_key(
      Keyboard *keyboard,
      uint32_t time,
      uint32_t xkb_scancode,
      bool keymap_key_repeats,
      uint32_t state,
      int xdg_key_symbol_count,
      const xkb_keysym_t *xdg_key_symbols) = 0;
  };

  class Keyboard {
  public:
    struct event_mask {
      bool enabled;
      bool all;
    };

    explicit Keyboard(event_mask const &event_mask,
                      const char *xkbmodel,
                      const char *xkblayout,
                      const char *xkbvariant,
                      const char *xkboptions,
                      int32_t delay = 500,
                      int32_t repeat = 33);

    ~Keyboard();

    void register_observer(KeyboardObserver *observer, void *user_data = nullptr);

    void unregister_observer(KeyboardObserver *observer);

    void set_user_data(void *user_data) { user_data_ = user_data; }

    [[nodiscard]] void *get_user_data() const { return user_data_; }

    void handle_keyboard_event(libinput_event_keyboard *key_event);

    void set_event_mask(event_mask const &event_mask);

    // Disallow copy and assign.
    Keyboard(const Keyboard &) = delete;

    Keyboard &operator=(const Keyboard &) = delete;

  private:
    std::list<KeyboardObserver *> observers_{};
    std::mutex observers_mutex_;
    void *user_data_{};
    event_mask event_mask_{};

    xkb_context *xkb_context_{};
    xkb_keymap *xkb_keymap_{};
    xkb_state *xkb_state_{};

    struct {
      int32_t rate;
      int32_t delay;
      timer_t timer;
      uint32_t code;
      sigevent sev;
      struct sigaction sa;

      struct {
        uint32_t serial;
        uint32_t time;
        uint32_t xkb_scancode;
        int key_repeats;
        int xdg_keysym_count;
        const xkb_keysym_t *key_syms;
      } notify;
    } repeat_{};

    void load_default_keymap();

    void load_keymap_from_file(const char *keymap_file = nullptr);

    static void xkb_log(xkb_context *context,
                        xkb_log_level level,
                        const char *format,
                        va_list args);

    void handle_repeat_info(int32_t delay, int32_t rate);

    static void repeat_xkb_v1_key_callback(int sig, siginfo_t *si, void *uc);

    static std::pair<std::string, std::string> get_keymap_filepath();
  };
} // namespace drmpp::input

#endif  // INCLUDE_DRMPP_INPUT_KEYBOARD_H_
