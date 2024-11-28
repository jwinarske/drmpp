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

#include "utils/virtual_terminal.h"

#include <csignal>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <linux/kd.h>
#include <linux/major.h>
#include <linux/vt.h>
#include <sys/sysmacros.h>

#include "logging/logging.h"

namespace drmpp::utils {
    termios VirtualTerminal::gPreviousTio;

    VirtualTerminal::VirtualTerminal() {
        // save current terminal settings
        tcgetattr(STDIN_FILENO, &gPreviousTio);

        // check if stdin is a tty
        struct stat buf{};
        if (fstat(STDIN_FILENO, &buf) == -1) {
            LOG_DEBUG("Not running on a vt");
            return;
        }

        // check if stdin is a tty
        if (major(buf.st_rdev) != TTY_MAJOR) {
            LOG_ERROR("stdin not a tty, running in no-display mode");
            return;
        }

        // disable canonical mode and echo
        termios tio = gPreviousTio;
        tio.c_lflag &= ~(ICANON | ECHO);
        tcsetattr(STDIN_FILENO, TCSANOW, &tio);

        // block VT switching
        vt_mode mode{VT_PROCESS, 0, 0};
        if (ioctl(STDIN_FILENO, VT_SETMODE, &mode) == -1) {
            LOG_ERROR("Failed to set vt handling");
            return;
        }

        // disable fb console
        if (ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS) == -1) {
            LOG_ERROR("Failed to switch console to graphics mode");
            return;
        }
    }

    VirtualTerminal::~VirtualTerminal() {
        restore();
    }

    void VirtualTerminal::restore() {
        vt_mode mode{};
        ioctl(STDIN_FILENO, VT_SETMODE, &mode);
        tcsetattr(STDIN_FILENO, TCSANOW, &gPreviousTio);
        ioctl(STDIN_FILENO, KDSETMODE, KD_TEXT);
    }
} // namespace drmpp::utils
