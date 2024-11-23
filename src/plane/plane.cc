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

#include <drm_fourcc.h>
#include <sys/mman.h>
#include <xf86drm.h>
#include <cassert>

#include "plane/plane.h"

namespace drmpp::plane {
drmModeConnector* Common::pick_connector(const int drm_fd,
                                         const drmModeRes* drm_res) {
  for (auto i = 0; i < drm_res->count_connectors; i++) {
    const auto connector = drmModeGetConnector(drm_fd, drm_res->connectors[i]);
    if (connector->connection == DRM_MODE_CONNECTED) {
      return connector;
    }
    drmModeFreeConnector(connector);
  }

  return nullptr;
}

drmModeCrtc* Common::pick_crtc(const int drm_fd,
                               const drmModeRes* drm_res,
                               const drmModeConnector* connector) {
  uint32_t crtc_id{};

  auto enc = drmModeGetEncoder(drm_fd, connector->encoder_id);
  if (enc) {
    // Current CRTC happens to be usable on the selected connector
    crtc_id = enc->crtc_id;
    drmModeFreeEncoder(enc);
    return drmModeGetCrtc(drm_fd, crtc_id);
  }

  // Current CRTC used by this encoder can't drive the selected connector.
  // Search all of them for a valid combination.
  int i, j;
  bool found;
  for (i = 0, found = false; !found && i < connector->count_encoders; i++) {
    enc = drmModeGetEncoder(drm_fd, connector->encoders[i]);

    if (!enc) {
      continue;
    }

    for (j = 0; !found && j < drm_res->count_crtcs; j++) {
      // Can the CRTC drive the connector?
      if (enc->possible_crtcs & (1 << j)) {
        crtc_id = drm_res->crtcs[j];
        found = true;
      }
    }
    drmModeFreeEncoder(enc);
  }

  if (found) {
    return drmModeGetCrtc(drm_fd, crtc_id);
  }
  return nullptr;
}

void Common::disable_all_crtcs_except(const int drm_fd,
                                      const drmModeRes* drm_res,
                                      const uint32_t crtc_id) {
  for (int i = 0; i < drm_res->count_crtcs; i++) {
    if (drm_res->crtcs[i] == crtc_id) {
      continue;
    }
    drmModeSetCrtc(drm_fd, drm_res->crtcs[i], 0, 0, 0, nullptr, 0, nullptr);
  }
}

bool Common::dumb_fb_init(dumb_fb* fb,
                          const int drm_fd,
                          const uint32_t format,
                          const uint32_t width,
                          const uint32_t height) {
  assert(format == DRM_FORMAT_ARGB8888 || format == DRM_FORMAT_XRGB8888);

  drm_mode_create_dumb create = {
      .height = height,
      .width = width,
      .bpp = 32,
      .flags = 0,
  };

  auto ret = drmIoctl(drm_fd, DRM_IOCTL_MODE_CREATE_DUMB, &create);
  if (ret < 0) {
    return false;
  }

  uint32_t handles[4]{};
  handles[0] = create.handle;
  uint32_t strides[4]{};
  strides[0] = create.pitch;
  constexpr uint32_t offsets[4]{};
  uint32_t fb_id;
  ret = drmModeAddFB2(drm_fd, width, height, format, handles, strides, offsets,
                      &fb_id, 0);
  if (ret < 0) {
    return false;
  }

  fb->width = width;
  fb->height = height;
  fb->stride = create.pitch;
  fb->size = create.size;
  fb->handle = create.handle;
  fb->id = fb_id;
  return true;
}

void* Common::dumb_fb_map(dumb_fb const* fb, const int drm_fd) {
  drm_mode_map_dumb map = {.handle = fb->handle};
  if (drmIoctl(drm_fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) {
    return MAP_FAILED;
  }

  return mmap(nullptr, fb->size, PROT_READ | PROT_WRITE, MAP_SHARED, drm_fd,
              static_cast<off_t>(map.offset));
}

void Common::dumb_fb_fill(Common::dumb_fb const* fb,
                          const int drm_fd,
                          const uint32_t color) {
  const auto data = static_cast<unsigned int*>(Common::dumb_fb_map(fb, drm_fd));
  if (data == MAP_FAILED) {
    return;
  }

  for (size_t i = 0; i < fb->size / sizeof(uint32_t); i++) {
    data[i] = color;
  }

  munmap(data, fb->size);
}
}  // namespace drmpp::plane
