/*
 * Copyright 2024 drmpp contributors
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

#ifndef INCLUDE_DRMPP_DRM_DEVICE_H_
#define INCLUDE_DRMPP_DRM_DEVICE_H_

#include <fcntl.h>

#include <optional>
#include <string>

#include <drm_fourcc.h>
extern "C" {
#include <libliftoff.h>
}
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "buffer.h"

namespace drmpp {

class Output;

class Device {
 public:
  explicit Device(int fd, struct gbm_device* gbm_device)
      : fd_(fd),
        gbm_device_(gbm_device, gbm_device_destroy),
        liftoff_device_(liftoff_device_create(fd), liftoff_device_destroy) {
    liftoff_device_register_all_planes(liftoff_device_.get());

    supportsModifiers_ = try_get_cap(DRM_CAP_ADDFB2_MODIFIERS).value_or(0) != 0;

    const auto cursor_width = try_get_cap(DRM_CAP_CURSOR_WIDTH);
    const auto cursor_height = try_get_cap(DRM_CAP_CURSOR_HEIGHT);
    if (cursor_width && cursor_height) {
      cursor_size_ = {cursor_width.value(), cursor_height.value()};
    }
  }

  virtual ~Device();

  // Disallow copy, copy-assign, move, move-assign
  Device(const Device&) = delete;
  Device& operator=(const Device&) = delete;
  Device(Device&&) = delete;
  Device& operator=(Device&&) = delete;

  // The fd can be used to change internal state of this device,
  // so we only provide non-const access.
  // NOLINTNEXTLINE(readability-make-member-function-const)
  [[nodiscard]] int fd() { return fd_; }
  struct gbm_device* gbm_device() { return gbm_device_.get(); }
  struct liftoff_device* liftoff_device() { return liftoff_device_.get(); }

  [[nodiscard]] bool supportsModifiers() const { return supportsModifiers_; }

  [[nodiscard]] std::optional<std::pair<uint32_t, uint32_t>> desiredCursorSize()
      const {
    return cursor_size_;
  }

  /**
   * @brief Create a DRM buffer object using DRM_IOCTL_MODE_CREATE_DUMB.
   *
   * DRM_IOCTL_MODE_CREATE_DUMB can only be called on a modesetting node
   * (/dev/dri/cardX) and the created buffer can only be used with the specific
   * node it was created on.
   *
   * It's not supposed to be used for rendering (although sometimes, this may
   * work), and sometimes even writing to it is not straightforward. It is
   * guaranteed however that scanning it out will work.
   *
   * @param width The width of the buffer.
   * @param height The height of the buffer.
   * @param bpp The bits per pixel of the buffer.
   * @param format The format of the buffer. (Used when later adding it as a
   * framebuffer)
   * @param modifier The modifier of the buffer. This is used to initialize
   *                 the modifier() field of the buffer.
   *                 If DRM_FORMAT_MOD_LINEAR is specified here, registering
   *                 the buffer as a framebuffer might fail if the driver
   *                 doesn't support modifiers.
   */
  std::unique_ptr<Buffer> createDumbBuffer(
      uint32_t width,
      uint32_t height,
      uint32_t bpp,
      uint32_t format,
      uint64_t modifier = DRM_FORMAT_MOD_INVALID);

  /**
   * @brief Create a DRM buffer object using GBM.
   *
   * Internally delegates to gbm_bo_create_with_modifiers2,
   * gbm_bo_create_with_modifiers and gbm_bo_create.
   *
   * GBM BO creation has a bit of format negotation built in. If given
   * a list of modifiers, it will try to select one that works with the
   * the given format & usage flags.
   *
   * @param width The width of the buffer.
   * @param height The height of the buffer.
   * @param format The format of the buffer. (Used when later adding it as a
   * framebuffer)
   * @param allowed_modifiers A list of allowed modifiers for the buffer.
   *                          If empty, explicit modifiers won't be used.
   * @param usage The usage flags for the buffer.
   */
  std::unique_ptr<Buffer> createGbmBuffer(
      uint32_t width,
      uint32_t height,
      uint32_t format,
      const std::vector<uint64_t>& allowed_modifiers,
      uint32_t usage);

  /**
   * @brief Register a raw buffer object as a framebuffer to the KMS device,
   *        which will give it a framebuffer ID.
   *
   * @param buffer The raw buffer to create the framebuffer from.
   * @returns A unique pointer to the created framebuffer.
   */
  std::unique_ptr<Framebuffer> addFramebuffer(Buffer& buffer);

  /**
   * @brief Create a DrmOutput of the first connected
   *        connector & CRTC of the device.
   */
  std::unique_ptr<Output> openFirstConnectedOutput();

  static std::unique_ptr<Device> create(int drm_fd) {
    auto gbm_device = gbm_create_device(drm_fd);
    if (!gbm_device) {
      return nullptr;
    }

    int ok = drmSetClientCap(drm_fd, DRM_CLIENT_CAP_ATOMIC, 1);
    if (ok < 0) {
      return nullptr;
    }

    return std::make_unique<Device>(drm_fd, gbm_device);
  }

  static std::unique_ptr<Device> open(const std::string& path) {
    auto fd = ::open(path.c_str(), O_RDWR | O_CLOEXEC);
    if (fd < 0) {
      return nullptr;
    }

    return create(fd);
  }

 private:
  const int fd_;

  bool supportsModifiers_;
  std::optional<std::pair<uint32_t, uint32_t>> cursor_size_;

  std::unique_ptr<struct gbm_device, decltype(&::gbm_device_destroy)>
      gbm_device_;

  std::unique_ptr<struct liftoff_device, decltype(&::liftoff_device_destroy)>
      liftoff_device_;

  [[nodiscard]] std::optional<uint64_t> try_get_cap(uint64_t cap) const;
};

}  // namespace drmpp

#endif  // INCLUDE_DRMPP_DEVICE_H_
