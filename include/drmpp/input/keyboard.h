#ifndef INCLUDE_DRMPP_INPUT_KEYBOARD_H_
#define INCLUDE_DRMPP_INPUT_KEYBOARD_H_

#include <utility>
#include <string>

#include <csignal>
#include <libinput.h>
#include <xkbcommon/xkbcommon.h>

namespace drmpp::input {
    class Keyboard {
    public:
        explicit Keyboard(int32_t delay = 500, int32_t repeat = 33);

        ~Keyboard();

        void handle_key(libinput_event_keyboard *key_event);

        // Disallow copy and assign.
        Keyboard(const Keyboard &) = delete;

        Keyboard &operator=(const Keyboard &) = delete;

    private:
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

        void load_keymap(const char *keymap_file = nullptr);

        static void xkb_log(xkb_context *context, xkb_log_level level, const char *format, va_list args);

        void handle_repeat_info(int32_t delay, int32_t rate);

        static void repeat_xkb_v1_key_callback(int sig, siginfo_t *si, void *uc);

        static std::pair<std::string, std::string> get_keymap_filepath();
    };
}

#endif // INCLUDE_DRMPP_INPUT_KEYBOARD_H_
