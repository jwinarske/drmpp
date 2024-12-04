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

#ifndef INCLUDE_DRMPP_BUFFER_H_
#define INCLUDE_DRMPP_BUFFER_H_

#include <gbm.h>
#include <bitset>
#include <cstdint>
#include <memory>
#include <vector>

namespace drmpp {

class Buffer {
 public:
  struct Plane {
    uint32_t gem_handle;
    uint32_t offset;
    uint32_t stride;
    uint64_t size;
  };

  explicit Buffer(uint32_t width,
                  uint32_t height,
                  uint32_t format,
                  uint64_t modifier,
                  const Plane& plane)
      : width_(width),
        height_(height),
        format_(format),
        modifier_(modifier),
        planes_({plane}) {}

  explicit Buffer(uint32_t width,
                  uint32_t height,
                  uint32_t format,
                  uint64_t modifier,
                  std::initializer_list<Plane> planes)
      : width_(width),
        height_(height),
        format_(format),
        modifier_(modifier),
        planes_(planes) {}

  virtual ~Buffer() = default;

  // Disallow copy, copy-assign, move, move-assign
  Buffer(const Buffer&) = delete;
  Buffer& operator=(const Buffer&) = delete;
  Buffer(Buffer&&) = delete;
  Buffer& operator=(Buffer&&) = delete;

  [[nodiscard]] uint32_t width() const { return width_; }
  [[nodiscard]] uint32_t height() const { return height_; }
  [[nodiscard]] uint32_t format() const { return format_; }

  /**
   * @brief The format modifier (e.g. DRM_FORMAT_MOD_LINEAR) of the buffer.
   *
   * If the modifier is DRM_FORMAT_MOD_INVALID, the buffer could be linear.
   * Especially dumb buffers will almost certainly be linear. For GBM buffers,
   * the actual memory layout is undefined in that case. It could be linear,
   * but it could also be something driver-specific.
   *
   * @return The format modifier of the buffer.
   */
  [[nodiscard]] uint64_t modifier() const { return modifier_; }

  [[nodiscard]] std::size_t n_planes() const { return planes_.size(); }

  // Plane iterator
  using const_iterator = std::vector<Plane>::const_iterator;
  [[nodiscard]] const_iterator begin() const { return planes_.begin(); }
  [[nodiscard]] const_iterator end() const { return planes_.end(); }

  /**
   * @brief Map the buffer for read/write CPU access.
   *
   * The buffer is always mapped linearly, regardless of the actual layout of
   * the buffer in memory.
   *
   * @param offset The offset of the mapped memory.
   * @param stride The stride of the mapped memory.
   * @param size The size of the mapped memory.
   * @param plane_index The index of the plane to map.
   */
  [[nodiscard]] virtual void* map(uint32_t& offset,
                                  uint32_t& stride,
                                  std::size_t& size,
                                  std::size_t plane_index) = 0;

  [[nodiscard]] void* map(uint32_t& offset,
                          uint32_t& stride,
                          std::size_t& size) {
    return map(offset, stride, size, 0);
  }

  virtual void unmap(void* memory) = 0;

  virtual void fill(uint32_t fourByteColor);

 protected:
  /**
   * @brief Constructor that doesn't initialize the planes_ vector.
   */
  explicit Buffer(uint32_t width,
                  uint32_t height,
                  uint32_t format,
                  uint64_t modifier)
      : width_(width), height_(height), format_(format), modifier_(modifier) {}

  const uint32_t width_;
  const uint32_t height_;
  const uint32_t format_;
  const uint64_t modifier_;
  std::vector<Plane> planes_;
};

class Framebuffer : public Buffer {
 public:
  explicit Framebuffer(Buffer& buffer, uint32_t fb_id)
      : Buffer(buffer.width(),
               buffer.height(),
               buffer.format(),
               buffer.modifier()),
        inner_(buffer),
        fb_id_(fb_id) {
    planes_.reserve(buffer.n_planes());
    for (const auto& plane : buffer) {
      planes_.push_back(plane);
    }
  }

  [[nodiscard]] inline uint32_t fb_id() const { return fb_id_; }

  [[nodiscard]] void* map(uint32_t& offset,
                          uint32_t& stride,
                          std::size_t& size,
                          std::size_t plane_index) override {
    return inner_.map(offset, stride, size, plane_index);
  }

  void unmap(void* memory) override { inner_.unmap(memory); }

 private:
  Buffer& inner_;
  const uint32_t fb_id_;
};

}  // namespace drmpp::drm

#endif  // INCLUDE_DRMPP_BUFFER_H_
