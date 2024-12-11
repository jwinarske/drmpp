/*
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bs_drm.h"

#define MAX_PLANE_COUNT 4

struct bs_drm_fb_builder {
  int fd;
  uint32_t width;
  uint32_t height;
  uint32_t format;
  size_t plane_count;
  uint32_t handles[MAX_PLANE_COUNT];
  uint32_t strides[MAX_PLANE_COUNT];
  uint32_t offsets[MAX_PLANE_COUNT];
  uint64_t modifier;
};

void bs_drm_fb_builder_init(struct bs_drm_fb_builder* self) {
  assert(self);
  self->fd = -1;
  self->modifier = DRM_FORMAT_MOD_INVALID;
}

struct bs_drm_fb_builder* bs_drm_fb_builder_new() {
  struct bs_drm_fb_builder* self = calloc(1, sizeof(struct bs_drm_fb_builder));
  assert(self);
  bs_drm_fb_builder_init(self);
  return self;
}

void bs_drm_fb_builder_destroy(struct bs_drm_fb_builder** self) {
  assert(self);
  assert(*self);
  free(*self);
  *self = NULL;
}

void bs_drm_fb_builder_gbm_bo(struct bs_drm_fb_builder* self,
                              struct gbm_bo* bo) {
  assert(self);
  assert(bo);
  struct gbm_device* gbm = gbm_bo_get_device(bo);
  assert(gbm);
  self->fd = gbm_device_get_fd(gbm);
  self->width = gbm_bo_get_width(bo);
  self->height = gbm_bo_get_height(bo);
  self->format = gbm_bo_get_format(bo);
  self->plane_count = gbm_bo_get_plane_count(bo);
  self->modifier = gbm_bo_get_modifier(bo);
  if (self->plane_count > MAX_PLANE_COUNT) {
    bs_debug_print(
        "WARNING", __func__, __FILE__, __LINE__,
        "only using first %d planes out of buffer object's %zu planes",
        MAX_PLANE_COUNT, self->plane_count);
    self->plane_count = MAX_PLANE_COUNT;
  }
  for (size_t plane_index = 0; plane_index < self->plane_count; plane_index++) {
    self->handles[plane_index] =
        gbm_bo_get_handle_for_plane(bo, plane_index).u32;
    self->strides[plane_index] = gbm_bo_get_stride_for_plane(bo, plane_index);
    self->offsets[plane_index] = gbm_bo_get_offset(bo, plane_index);
  }
}

void bs_drm_fb_builder_format(struct bs_drm_fb_builder* self, uint32_t format) {
  assert(self);
  self->format = format;
}

uint32_t bs_drm_fb_builder_create_fb(struct bs_drm_fb_builder* self) {
  assert(self);
  if (self->fd < 0) {
    bs_debug_error("failed to create drm fb: card fd %d is invalid", self->fd);
    return 0;
  }
  if (self->width <= 0 || self->height <= 0) {
    bs_debug_error("failed to create drm fb: dimensions %ux%u are invalid",
                   self->width, self->height);
    return 0;
  }
  if (self->format == 0) {
    bs_debug_error("failed to create drm fb: 0 format is invalid");
    return 0;
  }
  // Intel cannot scanout AR30/AB30 formats, but can do the X variants.
  unsigned format = self->format;
  if (format == GBM_FORMAT_ARGB2101010)
    format = GBM_FORMAT_XRGB2101010;
  if (format == GBM_FORMAT_ABGR2101010)
    format = GBM_FORMAT_XBGR2101010;
  if (self->plane_count == 0 || self->plane_count > MAX_PLANE_COUNT) {
    bs_debug_error("failed to create drm fb: plane count %zu is invalid\n",
                   self->plane_count);
    return 0;
  }
  for (size_t plane_index = self->plane_count; plane_index < MAX_PLANE_COUNT;
       plane_index++) {
    self->handles[plane_index] = 0;
    self->strides[plane_index] = 0;
    self->offsets[plane_index] = 0;
  }
  uint32_t fb_id;
  int ret = 0;
  if (self->modifier != DRM_FORMAT_MOD_INVALID &&
      bs_are_modifier_supported(self->fd, DRM_PLANE_TYPE_PRIMARY)) {
    uint64_t modifiers[4] = {DRM_FORMAT_MOD_NONE, DRM_FORMAT_MOD_NONE,
                             DRM_FORMAT_MOD_NONE, DRM_FORMAT_MOD_NONE};
    for (int i = 0; i < self->plane_count; i++)
      modifiers[i] = self->modifier;
    ret = drmModeAddFB2WithModifiers(
        self->fd, self->width, self->height, format, self->handles,
        self->strides, self->offsets, modifiers, &fb_id, DRM_MODE_FB_MODIFIERS);
  } else {
    ret = drmModeAddFB2(self->fd, self->width, self->height, format,
                        self->handles, self->strides, self->offsets, &fb_id, 0);
  }
  if (ret) {
    bs_debug_error(
        "failed to create drm fb: %s returned %d",
        self->modifier ? "drmModeAddFB2WithModifiers" : "drmModeAddFB2", ret);
    return 0;
  }
  return fb_id;
}

uint32_t bs_drm_fb_create_gbm(struct gbm_bo* bo) {
  assert(bo);
  struct bs_drm_fb_builder builder;
  bs_drm_fb_builder_init(&builder);
  bs_drm_fb_builder_gbm_bo(&builder, bo);
  uint32_t fb_id = bs_drm_fb_builder_create_fb(&builder);
  if (!fb_id) {
    bs_debug_error("failed to create framebuffer from buffer object");
    return 0;
  }
  return fb_id;
}