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

#include <csignal>
#include <filesystem>
#include <iostream>

#include <fcntl.h>
#include <linux/fb.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <unistd.h>

#include <cxxopts.hpp>

#include "drmpp/utils/utils.h"
#include "drmpp/utils/virtual_terminal.h"

struct Configuration {};

static volatile bool gRunning = true;

class App final : public Logging, public drmpp::utils::VirtualTerminal {
 public:
  struct fb_info {
    std::string path;
    int fd;
    void* ptr;
    fb_fix_screeninfo fix;
    fb_var_screeninfo var;
    unsigned bytespp;
  };

  explicit App(const Configuration& /* config */) {
    auto props = drmpp::utils::get_udev_fb_sys_attributes();
    for (auto& [key, value] : props) {
      if (strcmp(key.c_str(), "DEVNAME") == 0) {
        fb_info_.path = value;
      }
    }

    if (fb_info_.path.empty()) {
      LOG_ERROR("fb device node not found.");
      exit(EXIT_FAILURE);
    }

    LOG_INFO("** {} **", fb_info_.path.c_str());

    fb_info_.fd = open(fb_info_.path.c_str(), O_RDWR);
    if (!fb_info_.fd) {
      LOG_CRITICAL("Failed to open: {}", fb_info_.path.c_str());
      exit(EXIT_FAILURE);
    }

    if (ioctl(fb_info_.fd, FBIOGET_FSCREENINFO, &fb_info_.fix)) {
      LOG_CRITICAL("{}");
      close(fb_info_.fd);
      exit(EXIT_FAILURE);
    }

    if (ioctl(fb_info_.fd, FBIOGET_VSCREENINFO, &fb_info_.var)) {
      LOG_CRITICAL("{}");
      close(fb_info_.fd);
      exit(EXIT_FAILURE);
    }

    LOG_INFO("Resolution: {}x{}", fb_info_.var.xres, fb_info_.var.yres);
    LOG_INFO("Virtual {}x{}", fb_info_.var.xres_virtual,
             fb_info_.var.yres_virtual);
    LOG_INFO("Dimension: {}mm x {}mm", fb_info_.var.width, fb_info_.var.height);
    LOG_INFO("line_length: {}", fb_info_.fix.line_length);

    fb_info_.ptr = mmap(nullptr,
                        static_cast<size_t>(fb_info_.var.yres_virtual) *
                            static_cast<size_t>(fb_info_.fix.line_length),
                        PROT_WRITE | PROT_READ, MAP_SHARED, fb_info_.fd, 0);

    assert(fb_info_.ptr != MAP_FAILED);
  }

  ~App() override {
    close(fb_info_.fd);
    munmap(fb_info_.ptr, static_cast<size_t>(fb_info_.var.yres_virtual) *
                             static_cast<size_t>(fb_info_.fix.line_length));
  }

  static void paint_pixels(void* image,
                           const int padding,
                           const int width,
                           const int height,
                           const uint32_t time) {
    auto pixel = static_cast<uint32_t*>(image);
    const int half_h = padding + (height - padding * 2) / 2;
    const int half_w = padding + (width - padding * 2) / 2;

    /// Squared radii thresholds
    auto or_ = (half_w < half_h ? half_w : half_h) - 8;
    auto ir = or_ - 32;
    or_ *= or_;
    ir *= ir;

    pixel += padding * width;
    for (auto y = padding; y < height - padding; y++) {
      const int y2 = (y - half_h) * (y - half_h);

      pixel += padding;
      for (auto x = padding; x < width - padding; x++) {
        uint32_t v;

        /// Squared distance from center
        int r2 = (x - half_w) * (x - half_w) + y2;

        if (r2 < ir)
          v = (static_cast<uint32_t>(r2 / 32) + time / 64) * 0x0080401;
        else if (r2 < or_)
          v = (static_cast<uint32_t>(y) + time / 32) * 0x0080401;
        else
          v = (static_cast<uint32_t>(x) + time / 16) * 0x0080401;
        v &= 0x00ffffff;

        /// Cross if compositor uses X from XRGB as alpha
        if (abs(x - y) > 6 && abs(x + y - height) > 6)
          v |= 0xff000000;

        *pixel++ = v;
      }

      pixel += padding;
    }
  }

  [[nodiscard]] bool run() const {
    while (gRunning) {
      const std::chrono::time_point<std::chrono::system_clock> now =
          std::chrono::system_clock::now();
      const auto duration = now.time_since_epoch();
      const auto millis =
          std::chrono::duration_cast<std::chrono::milliseconds>(duration)
              .count();

      paint_pixels(fb_info_.ptr, 20, static_cast<int>(fb_info_.var.xres),
                   static_cast<int>(fb_info_.var.yres), millis);
      return true;
    }
    return false;
  }

 private:
  fb_info fb_info_{};
};

int main(const int argc, char** argv) {
  std::signal(SIGINT, [](const int signal) {
    if (signal == SIGINT) {
      gRunning = false;
    }
  });

  cxxopts::Options options("drm-fb", "Query FB parameters");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()("help", "Print help");

  if (options.parse(argc, argv).count("help")) {
    spdlog::info("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  const App app({});

  while (app.run()) {
  }

  return EXIT_SUCCESS;
}
