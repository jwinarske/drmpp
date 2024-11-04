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

#include "output.h"

#include <cstring>

#include "composition.h"
#include "logging/logging.h"
#include "output.h"

namespace drmpp {

bool Output::present(const Composition& composition) {
  // For now, we do the stupid thing and recreate all layers every frame.
  layers_.clear();

  /// TODO: Allocate a composition layer (& buffer), and blit all layers that
  ///       didn't get a plane into it.

  uint64_t zpos = 0;
  for (const auto& layer : composition) {
    auto buffer = layer.buffer;
    auto src = layer.src;
    auto dst = layer.dst;

    /// TODO: Cache layers
    struct liftoff_layer* output_layer = ::liftoff_layer_create(output_.get());
    if (!output_layer) {
      LOG_ERROR("Failed to create liftoff layer. liftoff_layer_create: {}",
                std::strerror(ENOMEM));
      return false;
    }

    layers_.emplace_back(output_layer, ::liftoff_layer_destroy);

    uint32_t fb_id = addFramebuffer(buffer.get());

    ::liftoff_layer_set_property(output_layer, "FB_ID", fb_id);
    ::liftoff_layer_set_property(output_layer, "CRTC_X", dst.x);
    ::liftoff_layer_set_property(output_layer, "CRTC_Y", dst.y);
    ::liftoff_layer_set_property(output_layer, "CRTC_W", dst.w);
    ::liftoff_layer_set_property(output_layer, "CRTC_H", dst.h);
    ::liftoff_layer_set_property(output_layer, "SRC_X", src.x << 16);
    ::liftoff_layer_set_property(output_layer, "SRC_Y", src.y << 16);
    ::liftoff_layer_set_property(output_layer, "SRC_W", src.w << 16);
    ::liftoff_layer_set_property(output_layer, "SRC_H", src.h << 16);
    ::liftoff_layer_set_property(output_layer, "zpos", zpos);

    zpos++;
  }

  if (const auto* pointer = composition.pointerLayer()) {
    /* Add a layer for the pointer. */
    struct liftoff_layer* output_layer = ::liftoff_layer_create(output_.get());
    if (!output_layer) {
      LOG_ERROR("Failed to create liftoff layer. liftoff_layer_create: {}",
                std::strerror(ENOMEM));
      return false;
    }

    layers_.emplace_back(output_layer, ::liftoff_layer_destroy);

    // This conveniently returns 0 on error, which is the same as
    // setting liftoff_layer_set_needs_composition().
    uint32_t fb_id = addFramebuffer(pointer->buffer.get());

    // If the cursor image is partially offscreen, naively scanning out
    // the cursor at `cursor_x - hot_x` to `cursor_x - hot_x + cursor_width`
    // would result in presenting the cursor layer at a negative screen
    // position.
    //
    // Instead, we clip the screen and buffer positions in that case.
    //
    // These calculations are a bit easier with two coordinate pairs rather
    // than top left coordinate + w/h.
    int out_x1 =
        static_cast<int>(pointer->x) - static_cast<int>(pointer->hot_x);
    int out_y1 =
        static_cast<int>(pointer->y) - static_cast<int>(pointer->hot_y);
    int out_x2 = static_cast<int>(out_x1 + pointer->buffer->width());
    int out_y2 = static_cast<int>(out_y1 + pointer->buffer->height());

    int src_x1 = 0;
    int src_y1 = 0;
    int src_x2 = src_x1 + static_cast<int>(pointer->buffer->width());
    int src_y2 = src_y1 + static_cast<int>(pointer->buffer->height());

    if (out_x1 < 0) {
      const auto inset = -out_x1;
      out_x1 = 0;
      src_x1 += inset;
    }
    if (out_y1 < 0) {
      const auto inset = -out_y1;
      out_y1 = 0;
      src_y1 += inset;
    }
    if (out_x2 > static_cast<int>(width())) {
      const auto inset = out_x2 - static_cast<int>(width());
      out_x2 = static_cast<int>(width());
      src_x2 -= inset;
    }
    if (out_y2 > static_cast<int>(height())) {
      const auto inset = out_y2 - static_cast<int>(height());
      out_y2 = static_cast<int>(height());
      src_y2 -= inset;
    }

    ::liftoff_layer_set_property(output_layer, "FB_ID", fb_id);
    ::liftoff_layer_set_property(output_layer, "CRTC_X", out_x1);
    ::liftoff_layer_set_property(output_layer, "CRTC_Y", out_y1);
    ::liftoff_layer_set_property(output_layer, "CRTC_W", out_x2 - out_x1);
    ::liftoff_layer_set_property(output_layer, "CRTC_H", out_y2 - out_y1);
    ::liftoff_layer_set_property(output_layer, "SRC_X", src_x1 << 16);
    ::liftoff_layer_set_property(output_layer, "SRC_Y", src_y1 << 16);
    ::liftoff_layer_set_property(output_layer, "SRC_W",
                                 (src_x2 - src_x1) << 16);
    ::liftoff_layer_set_property(output_layer, "SRC_H",
                                 (src_y2 - src_y1) << 16);
    ::liftoff_layer_set_property(output_layer, "zpos", zpos);

    zpos++;
  }

  auto req = std::unique_ptr<drmModeAtomicReq, decltype(&::drmModeAtomicFree)>(
      ::drmModeAtomicAlloc(), ::drmModeAtomicFree);
  if (!req) {
    return false;
  }

  /// TODO: Implement non-blocking commits
  uint32_t flags = 0;
  if (!did_set_mode) {
    flags |= DRM_MODE_ATOMIC_ALLOW_MODESET;
  }

  /// TODO: Add composition layer.
  int ok = ::liftoff_output_apply(output_.get(), req.get(), flags, nullptr);
  if (ok < 0) {
    LOG_ERROR("Plane allocation failed. liftoff_output_apply: {}",
              std::strerror(-ok));
    return false;
  }

  /// TODO: Compose layers here if needed.
  if (!did_set_mode) {
    bool success = applyMode(req.get());
    if (!success) {
      return false;
    }
  }

  ok = ::drmModeAtomicCommit(device_.fd(), req.get(), flags, nullptr);
  if (ok != 0) {
    LOG_ERROR("Failed to commit atomic request. drmModeAtomicCommit: {}",
              std::strerror(-ok));
    return false;
  }

  did_set_mode = true;

  return true;
}

uint32_t Output::addFramebuffer(Buffer* buffer) {
  auto it = fb_cache_.find(buffer);
  if (it != fb_cache_.end()) {
    return it->second->fb_id();
  }

  auto fb = device_.addFramebuffer(*buffer);
  if (!fb) {
    return 0;
  }

  fb_cache_[buffer] = std::move(fb);
  return fb_cache_[buffer]->fb_id();
}

std::optional<Output::Props> Output::resolveProps(int fd, uint32_t crtc_id) {
  Props result = {0};

  auto crtc_res =
      ::drmModeObjectGetProperties(fd, crtc_id, DRM_MODE_OBJECT_CRTC);

  // find MODE_ID and ACTIVE properties
  for (uint32_t i = 0;
       i < crtc_res->count_props && (result.active == 0 || result.mode_id == 0);
       i++) {
    auto prop = ::drmModeGetProperty(fd, crtc_res->props[i]);
    if (!prop) {
      continue;
    }

    if (::strcmp(prop->name, "MODE_ID") == 0) {
      result.mode_id = crtc_res->props[i];
    } else if (::strcmp(prop->name, "ACTIVE") == 0) {
      result.active = crtc_res->props[i];
    }

    ::drmModeFreeProperty(prop);
  }

  ::drmModeFreeObjectProperties(crtc_res);

  if (result.mode_id == 0 || result.active == 0) {
    if (result.mode_id == 0 && result.active == 0) {
      LOG_ERROR("Failed to find MODE_ID and ACTIVE CRTC properties.");
    } else {
      LOG_ERROR("Failed to find {} CRTC property.",
                result.mode_id == 0 ? "MODE_ID" : "ACTIVE");
    }
    return std::nullopt;
  }

  return result;
}

bool Output::applyMode(drmModeAtomicReq* req) {
  uint32_t mode_blob_id;

  if (!crtc_props_) {
    crtc_props_ = resolveProps(device_.fd(), crtc_id_);
    if (!crtc_props_) {
      return false;
    }
  }

  int ok = ::drmModeCreatePropertyBlob(device_.fd(), &mode_, sizeof(mode_),
                                       &mode_blob_id);
  if (ok != 0) {
    LOG_ERROR("Failed to upload video mode. drmModeCreatePropertyBlob: {}",
              std::strerror(-ok));
    return false;
  }

  int cursor_before = drmModeAtomicGetCursor(req);

  ok = ::drmModeAtomicAddProperty(req, crtc_id_, crtc_props_->mode_id,
                                  mode_blob_id);
  if (ok < 0) {
    LOG_ERROR(
        "Failed to add MODE_ID property to atomic request. "
        "drmModeAtomicAddProperty: {}",
        std::strerror(-ok));
    drmModeDestroyPropertyBlob(device_.fd(), mode_blob_id);
    return false;
  }

  ok = ::drmModeAtomicAddProperty(req, crtc_id_, crtc_props_->active, 1);
  if (ok < 0) {
    LOG_ERROR(
        "Failed to add ACTIVE property to atomic request. "
        "drmModeAtomicAddProperty: {}",
        std::strerror(-ok));
    drmModeAtomicSetCursor(req, cursor_before);
    drmModeDestroyPropertyBlob(device_.fd(), mode_blob_id);
    return false;
  }

  mode_blob_id_ = mode_blob_id;
  return true;
}

}  // namespace drmpp
