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

#include <map>

#include <drm_fourcc.h>
#include <gbm.h>
#include <sys/mman.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include "device.h"
#include "plane/plane.h"

namespace drmpp {

class DumbBuffer : public Buffer {
 public:
  explicit DumbBuffer(Device& drm_device,
                      const uint32_t width,
                      const uint32_t height,
                      const uint32_t format,
                      const uint64_t modifier,
                      const Plane& plane)
      : Buffer(width, height, format, modifier, plane),
        drm_device_(drm_device) {}

  // Destructor removes dumb buffer.
  ~DumbBuffer() override;

  void* map(uint32_t& offset,
            uint32_t& stride,
            std::size_t& size,
            std::size_t plane_index) override;

  void unmap(void* memory) override;

 private:
  Device& drm_device_;
  std::map<void*, std::size_t> mappings_;
};

DumbBuffer::~DumbBuffer() {
  drm_mode_destroy_dumb destroy = {.handle = planes_[0].gem_handle};
  ::drmIoctl(drm_device_.fd(), DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}

void* DumbBuffer::map(uint32_t& offset,
                      uint32_t& stride,
                      std::size_t& size,
                      std::size_t plane_index) {
  if (plane_index >= planes_.size()) {
    return nullptr;
  }

  const Plane& plane = planes_[plane_index];

  drm_mode_map_dumb map = {.handle = plane.gem_handle};
  if (::drmIoctl(drm_device_.fd(), DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
    return nullptr;
  }

  void* result = ::mmap(nullptr, plane.size, PROT_READ | PROT_WRITE, MAP_SHARED,
                        drm_device_.fd(), static_cast<off_t>(map.offset));
  if (result == MAP_FAILED) {
    return nullptr;
  }

  mappings_[result] = plane.size;

  offset = plane.offset;
  stride = plane.stride;
  size = plane.size;
  return result;
}

void DumbBuffer::unmap(void* memory) {
  auto it = mappings_.find(memory);
  if (it == mappings_.end()) {
    return;
  }

  ::munmap(memory, it->second);

  mappings_.erase(it);
}

class GbmBuffer : public Buffer {
 public:
  explicit GbmBuffer(
      Device& drm_device,
      std::unique_ptr<struct gbm_bo, decltype(&::gbm_bo_destroy)> bo)
      : Buffer(::gbm_bo_get_width(bo.get()),
               ::gbm_bo_get_height(bo.get()),
               ::gbm_bo_get_format(bo.get()),
               ::gbm_bo_get_modifier(bo.get())),
        drm_device_(drm_device),
        bo_(std::move(bo)) {
    add_planes();
  }

  void* map(uint32_t& offset,
            uint32_t& stride,
            std::size_t& size,
            std::size_t plane_index) override;

  void unmap(void* memory) override;

 private:
  void add_planes();

  Device& drm_device_;
  std::unique_ptr<struct gbm_bo, decltype(&::gbm_bo_destroy)> bo_;
  std::map<void*, void*> mappings_;
};

void GbmBuffer::add_planes() {
  struct gbm_bo* bo = bo_.get();

  int n_planes = ::gbm_bo_get_plane_count(bo);
  planes_.reserve(n_planes);

  for (int i = 0; i < n_planes; i++) {
    /// TODO: This can fail
    gbm_bo_handle handle = ::gbm_bo_get_handle_for_plane(bo, i);
    uint32_t offset = ::gbm_bo_get_offset(bo, i);
    uint32_t stride = ::gbm_bo_get_stride_for_plane(bo, i);
    std::size_t size = offset + stride * ::gbm_bo_get_height(bo);

    planes_.push_back(Buffer::Plane{
        .gem_handle = handle.u32,
        .offset = offset,
        .stride = stride,
        .size = size,
    });
  }
}

void* GbmBuffer::map(uint32_t& offset,
                     uint32_t& stride,
                     std::size_t& size,
                     std::size_t plane_index) {
  void* userdata = nullptr;

  void* memory = ::gbm_bo_map(bo_.get(), 0, 0, width(), height(),
                              GBM_BO_TRANSFER_READ_WRITE, &stride, &userdata);
  if (!memory) {
    return nullptr;
  }

  mappings_[memory] = userdata;
  return memory;
}

void GbmBuffer::unmap(void* memory) {
  auto it = mappings_.find(memory);
  if (it == mappings_.end()) {
    return;
  }

  ::gbm_bo_unmap(bo_.get(), it->second);

  mappings_.erase(it);
}

class DrmFramebufferImpl : public Framebuffer {
 public:
  explicit DrmFramebufferImpl(Device& device,
                              Buffer& buffer,
                              const uint32_t fb_id)
      : Framebuffer(buffer, fb_id), device_(device) {}

  ~DrmFramebufferImpl() override { ::drmModeRmFB(device_.fd(), fb_id()); }

 private:
  Device& device_;
};

Device::~Device() {
  // Destroy the gbm_device before closing the fd.
  gbm_device_ = {};

  if (fd_ >= 0) {
    ::close(fd_);
  }
}

std::unique_ptr<Buffer> Device::createDumbBuffer(const uint32_t width,
                                                 const uint32_t height,
                                                 const uint32_t bpp,
                                                 const uint32_t format,
                                                 const uint64_t modifier) {
  drm_mode_create_dumb create = {
      .height = height,
      .width = width,
      .bpp = bpp,
      .flags = 0,
  };

  int ret = ::drmIoctl(fd_, DRM_IOCTL_MODE_CREATE_DUMB, &create);
  if (ret < 0) {
    return nullptr;
  }

  Buffer::Plane plane = {
      .gem_handle = create.handle,
      .offset = 0,
      .stride = create.pitch,
      .size = create.size,
  };

  auto bo = std::make_unique<DumbBuffer>(*this, width, height, format, modifier,
                                         plane);

  return bo;
}

std::unique_ptr<Buffer> Device::createGbmBuffer(
    const uint32_t width,
    const uint32_t height,
    const uint32_t format,
    const std::vector<uint64_t>& allowed_modifiers,
    const uint32_t usage) {
  struct gbm_bo* bo;
  if (allowed_modifiers.empty()) {
    bo = ::gbm_bo_create(gbm_device_.get(), width, height, format, usage);
  } else {
    bo = ::gbm_bo_create_with_modifiers2(gbm_device_.get(), width, height,
                                         format, allowed_modifiers.data(),
                                         allowed_modifiers.size(), usage);
  }

  if (bo == nullptr) {
    return nullptr;
  }

  // Destroy the BO with gbm_bo_gestroy.
  auto bo_ptr = std::unique_ptr<gbm_bo, decltype(&::gbm_bo_destroy)>(
      bo, ::gbm_bo_destroy);

  auto drm_bo = std::make_unique<GbmBuffer>(*this, std::move(bo_ptr));

  return drm_bo;
}

std::unique_ptr<Framebuffer> Device::addFramebuffer(Buffer& buffer) {
  uint32_t handles[4] = {0};
  uint32_t strides[4] = {0};
  uint32_t offsets[4] = {0};
  uint64_t modifiers[4] = {0};
  uint32_t fb_id;

  uint64_t modifier = buffer.modifier();
  bool have_modifier = modifier != DRM_FORMAT_MOD_INVALID;

  uint32_t flags = 0;
  if (have_modifier) {
    flags |= DRM_MODE_FB_MODIFIERS;
  }

  std::size_t index = 0;
  for (const auto& plane : buffer) {
    handles[index] = plane.gem_handle;
    strides[index] = plane.stride;
    offsets[index] = plane.offset;

    // The DRM_IOCTL_MODE_ADDFB2 ioctl expects the modifiers to be 0
    // (not DRM_FORMAT_MOD_INVALID) if  the DRM_MODE_FB_MODIFIERS flag
    // is not set.
    modifiers[index] = have_modifier ? modifier : 0;
    index++;
  }

  int ret = ::drmModeAddFB2WithModifiers(fd_, buffer.width(), buffer.height(),
                                         buffer.format(), handles, strides,
                                         offsets, modifiers, &fb_id, flags);

  if (ret < 0) {
    LOG_ERROR("Failed to add framebuffer. drmModeAddFB2WithModifiers: {}",
              std::strerror(-ret));
    return nullptr;
  }

  auto fb = std::make_unique<DrmFramebufferImpl>(*this, buffer, fb_id);
  return fb;
}

std::unique_ptr<Output> Device::openFirstConnectedOutput() {
  auto res = std::unique_ptr<drmModeRes, decltype(&::drmModeFreeResources)>(
      ::drmModeGetResources(fd_), ::drmModeFreeResources);
  if (!res) {
    /// TODO: Error message
    return nullptr;
  }

  const auto connector = drmpp::plane::Common::pick_connector(fd_, res.get());
  if (connector == nullptr) {
    /// TODO: Error message
    return nullptr;
  }

  const auto conn_id = connector->connector_id;

  const auto crtc = drmpp::plane::Common::pick_crtc(fd_, res.get(), connector);
  if (crtc == nullptr || !crtc->mode_valid) {
    /// TODO: Error message
    drmModeFreeConnector(connector);
    return nullptr;
  }

  const auto crtc_id = crtc->crtc_id;

  auto mode = crtc->mode;

  drmModeFreeConnector(connector);
  drmModeFreeCrtc(crtc);

  return std::make_unique<Output>(*this, crtc_id, conn_id, mode);
}

std::optional<uint64_t> Device::try_get_cap(const uint64_t cap) const {
  uint64_t value;
  if (drmGetCap(fd_, cap, &value) == 0) {
    return value;
  }

  return std::nullopt;
}

}  // namespace drmpp
