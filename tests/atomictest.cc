/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

/*
 * This file performs some sanity checks on the DRM atomic API. To run a test,
 * please run the following command:
 *
 * atomictest <testname>
 *
 * To get a list of possible tests, run:
 *
 * atomictest
 */

#include <getopt.h>
#include <pthread.h>
#include <../subprojects/sync/include/sync/sync.h>
#include <cerrno>
#include <cinttypes>
#include <cstring>

#include "../include/drmpp/shared_libs/libdrm.h"
#include "../include/drmpp/shared_libs/libgbm.h"

extern "C" {
#include "../subprojects/bsdrm/include/bs_drm.h"
}

#define CHECK(cond)                             \
  do {                                          \
    if (!(cond)) {                              \
      bs_debug_error("check %s failed", #cond); \
      return -1;                                \
    }                                           \
  } while (0)

#define CHECK_RESULT(ret)                           \
  do {                                              \
    if ((ret) < 0) {                                \
      bs_debug_error("failed with error: %d", ret); \
      return -1;                                    \
    }                                               \
  } while (0)

#define MAX(a, b) ((a) > (b) ? (a) : (b))

#define CURSOR_SIZE 64

#define DRM_MODE_ROTATE_0_LOG2 0

#define DRM_MODE_REFLECT_Y_LOG2 5

#define TEST_COMMIT_FAIL 1

#define GAMMA_MAX_VALUE ((1 << 16) - 1)

static constexpr uint32_t yuv_formats[] = {
  DRM_FORMAT_NV12,
  DRM_FORMAT_YVU420,
  DRM_FORMAT_YUYV,
};

/*
 * The blob for the CTM property is a drm_color_ctm.
 * drm_color_ctm contains a 3x3 u64 matrix. Every element is represented as
 * sign and U31.32. The sign is the MSB.
 */
// clang-format off
static int64_t identity_ctm[9] = {
	0x100000000, 0x0, 0x0,
	0x0, 0x100000000, 0x0,
	0x0, 0x0, 0x100000000
};
static int64_t red_shift_ctm[9] = {
	0x140000000, 0x0, 0x0,
	0x0, 0xC0000000, 0x0,
	0x0, 0x0, 0xC0000000
};
// clang-format on

static bool automatic = false;
static gbm_device *gbm_device = nullptr;

static void page_flip_handler(int /* fd */,
                              unsigned int /* sequence */,
                              unsigned int /* tv_sec */,
                              unsigned int /* tv_usec */,
                              void * /* user_data */) {
  // Nothing to do.
}

struct Property {
  uint32_t pid;
  mutable uint64_t value;
};

struct Plane {
  drmModePlane drm_plane;
  gbm_bo *bo;

  uint32_t format_idx;

  // Properties
  Property crtc_id;
  Property crtc_x;
  Property crtc_y;
  Property crtc_w;
  Property crtc_h;
  Property fb_id;
  Property src_x;
  Property src_y;
  Property src_w;
  Property src_h;
  Property type;
  Property in_fence_fd;
  Property rotation;
  Property ctm;
  Property alpha;
};

struct Connector {
  uint32_t connector_id;
  Property crtc_id;
  Property edid;
  Property dpms;
};

struct Crtc {
  uint32_t crtc_id;
  uint32_t width;
  uint32_t height;
  uint32_t *primary_idx;
  uint32_t *cursor_idx;
  uint32_t *overlay_idx;
  uint32_t num_primary;
  uint32_t num_cursor;
  uint32_t num_overlay;

  Plane *planes;
  Property mode_id;
  Property active;
  Property out_fence_ptr;
  Property ctm;
  Property gamma_lut;
  Property gamma_lut_size;
  Property background_color;
};

struct Mode {
  uint32_t height;
  uint32_t width;
  uint32_t id;
};

struct Context {
  int fd;
  uint32_t num_crtcs;
  uint32_t num_connectors;
  uint32_t num_modes;

  Connector *connectors;
  Crtc *crtcs;
  Mode *modes;
  drmModeAtomicReqPtr pset;
  drmModeResPtr res;
  drmEventContext drm_event_ctx;

  bs_mapper *mapper;

  uint64_t modifier;
};

typedef int (*test_function)(Context *ctx, const Crtc *crtc);

struct Testcase {
  const char *name;
  test_function test_func;
};

// clang-format off
enum draw_format_type {
	DRAW_NONE = 0,
	DRAW_STRIPE = 1,
	DRAW_TRANSPARENT_HOLE = 2,
	DRAW_ELLIPSE = 3,
	DRAW_CURSOR = 4,
	DRAW_LINES = 5,
};
// clang-format on

static int drmModeCreatePropertyBlob64(const int fd,
                                       const void *data,
                                       const size_t length,
                                       uint64_t *id) {
  uint32_t ctm_blob_id = 0;
  const int ret = drm->ModeCreatePropertyBlob(fd, data, length, &ctm_blob_id);
  *id = ctm_blob_id;
  return ret;
}

static int drmModeDestroyPropertyBlob64(const int fd, const uint64_t id) {
  CHECK(id < (1ull << 32));
  return drm->ModeDestroyPropertyBlob(fd, static_cast<uint32_t>(id));
}

static int32_t get_format_idx(const Plane *plane,
                              const uint32_t format) {
  for (int32_t i = 0; i < static_cast<int32_t>(plane->drm_plane.count_formats);
       i++)
    if (plane->drm_plane.formats[i] == format)
      return i;
  return -1;
}

static void copy_drm_plane(drmModePlane *dest, const drmModePlane *src) {
  memcpy(dest, src, sizeof(drmModePlane));
  dest->formats =
      static_cast<uint32_t *>(calloc(src->count_formats, sizeof(uint32_t)));
  memcpy(dest->formats, src->formats, src->count_formats * sizeof(uint32_t));
}

static Plane *get_plane(const Crtc *crtc,
                        const uint32_t idx,
                        const uint64_t type) {
  uint32_t index;
  switch (type) {
    case DRM_PLANE_TYPE_OVERLAY:
      index = crtc->overlay_idx[idx];
      break;
    case DRM_PLANE_TYPE_PRIMARY:
      index = crtc->primary_idx[idx];
      break;
    case DRM_PLANE_TYPE_CURSOR:
      index = crtc->cursor_idx[idx];
      break;
    default:
      bs_debug_error("invalid plane type returned");
      return nullptr;
  }

  return &crtc->planes[index];
}

static int draw_to_plane(bs_mapper *mapper,
                         const Plane *plane,
                         const draw_format_type pattern) {
  gbm_bo *bo = plane->bo;
  const uint32_t format = gbm->bo_get_format(bo);

  if (const bs_draw_format *draw_format = bs_get_draw_format(format);
    draw_format && pattern) {
    switch (pattern) {
      case DRAW_STRIPE:
        CHECK(bs_draw_stripe(mapper, bo, draw_format));
        break;
      case DRAW_TRANSPARENT_HOLE:
        CHECK(bs_draw_transparent_hole(mapper, bo, draw_format));
        break;
      case DRAW_ELLIPSE:
        CHECK(bs_draw_ellipse(mapper, bo, draw_format, 0));
        break;
      case DRAW_CURSOR:
        CHECK(bs_draw_cursor(mapper, bo, draw_format));
        break;
      case DRAW_LINES:
        CHECK(bs_draw_lines(mapper, bo, draw_format));
        break;
      default:
        bs_debug_error("invalid draw type");
        return -1;
    }
  } else {
    // DRM_FORMAT_RGB565 --> red, DRM_FORMAT_BGR565 --> blue,
    // everything else --> something
    void *map_data;
    constexpr uint16_t value = 0xF800;
    uint32_t stride;
    void *addr = bs_mapper_map(mapper, bo, 0, &map_data, &stride);
    const uint64_t num_shorts =
        static_cast<uint64_t>(stride) * gbm->bo_get_height(bo) / sizeof(uint16_t);
    auto *pixel = static_cast<uint16_t *>(addr);

    CHECK(addr);
    for (uint32_t i = 0; i < num_shorts; i++)
      pixel[i] = value;

    bs_mapper_unmap(mapper, bo, map_data);
  }

  return 0;
}

static int get_prop(const int fd,
                    const drmModeObjectProperties *props,
                    const char *name,
                    Property *bs_prop) {
  // Property ID should always be > 0
  bs_prop->pid = 0;
  for (uint32_t i = 0; i < props->count_props; i++) {
    if (bs_prop->pid)
      break;

    if (const auto prop = drm->ModeGetProperty(fd, props->props[i])) {
      if (!strcmp(prop->name, name)) {
        bs_prop->pid = prop->prop_id;
        bs_prop->value = props->prop_values[i];
      }
      drm->ModeFreeProperty(prop);
    }
  }

  return (bs_prop->pid == 0) ? -1 : 0;
}

static int get_connector_props(const int fd,
                               Connector *connector,
                               const drmModeObjectProperties *props) {
  CHECK_RESULT(get_prop(fd, props, "CRTC_ID", &connector->crtc_id));
  CHECK_RESULT(get_prop(fd, props, "EDID", &connector->edid));
  CHECK_RESULT(get_prop(fd, props, "DPMS", &connector->dpms));
  return 0;
}

static int get_crtc_props(const int fd,
                          Crtc *crtc,
                          const drmModeObjectProperties *props) {
  CHECK_RESULT(get_prop(fd, props, "MODE_ID", &crtc->mode_id));
  CHECK_RESULT(get_prop(fd, props, "ACTIVE", &crtc->active));
  CHECK_RESULT(get_prop(fd, props, "OUT_FENCE_PTR", &crtc->out_fence_ptr));

  /*
   * The atomic API makes no guarantee a property is present in object. This
   * test requires the above common properties since a plane is undefined
   * without them. Other properties (i.e: ctm) are optional.
   */
  get_prop(fd, props, "CTM", &crtc->ctm);
  get_prop(fd, props, "GAMMA_LUT", &crtc->gamma_lut);
  get_prop(fd, props, "GAMMA_LUT_SIZE", &crtc->gamma_lut_size);
  get_prop(fd, props, "BACKGROUND_COLOR", &crtc->background_color);

  return 0;
}

static int get_plane_props(const int fd,
                           Plane *plane,
                           const drmModeObjectProperties *props) {
  CHECK_RESULT(get_prop(fd, props, "CRTC_ID", &plane->crtc_id));
  CHECK_RESULT(get_prop(fd, props, "FB_ID", &plane->fb_id));
  CHECK_RESULT(get_prop(fd, props, "CRTC_X", &plane->crtc_x));
  CHECK_RESULT(get_prop(fd, props, "CRTC_Y", &plane->crtc_y));
  CHECK_RESULT(get_prop(fd, props, "CRTC_W", &plane->crtc_w));
  CHECK_RESULT(get_prop(fd, props, "CRTC_H", &plane->crtc_h));
  CHECK_RESULT(get_prop(fd, props, "SRC_X", &plane->src_x));
  CHECK_RESULT(get_prop(fd, props, "SRC_Y", &plane->src_y));
  CHECK_RESULT(get_prop(fd, props, "SRC_W", &plane->src_w));
  CHECK_RESULT(get_prop(fd, props, "SRC_H", &plane->src_h));
  CHECK_RESULT(get_prop(fd, props, "type", &plane->type));
  CHECK_RESULT(get_prop(fd, props, "IN_FENCE_FD", &plane->in_fence_fd));

  /*
   * The atomic API makes no guarantee a property is present in object. This
   * test requires the above common properties since a plane is undefined
   * without them. Other properties (i.e: rotation and ctm) are optional.
   */
  get_prop(fd, props, "rotation", &plane->rotation);
  get_prop(fd, props, "PLANE_CTM", &plane->ctm);
  get_prop(fd, props, "alpha", &plane->alpha);
  return 0;
}

static int set_connector_props(const Connector *conn,
                               drmModeAtomicReq *pset) {
  const uint32_t id = conn->connector_id;
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, conn->crtc_id.pid,
    conn->crtc_id.value));
  return 0;
}

static int set_crtc_props(const Crtc *crtc,
                          drmModeAtomicReq *pset) {
  const uint32_t id = crtc->crtc_id;
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, crtc->mode_id.pid,
    crtc->mode_id.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, crtc->active.pid,
    crtc->active.value));
  if (crtc->out_fence_ptr.value)
    CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, crtc->out_fence_ptr.pid,
    crtc->out_fence_ptr.value));
  if (crtc->ctm.pid)
    CHECK_RESULT(
    drm->ModeAtomicAddProperty(pset, id, crtc->ctm.pid, crtc->ctm.value));
  if (crtc->gamma_lut.pid)
    CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, crtc->gamma_lut.pid,
    crtc->gamma_lut.value));
  if (crtc->background_color.pid) {
    CHECK_RESULT(drm->ModeAtomicAddProperty(
      pset, id, crtc->background_color.pid, crtc->background_color.value));
  }

  return 0;
}

static int set_plane_props(const Plane *plane,
                           drmModeAtomicReq *pset) {
  const uint32_t id = plane->drm_plane.plane_id;
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->crtc_id.pid,
    plane->crtc_id.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->fb_id.pid,
    plane->fb_id.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->crtc_x.pid,
    plane->crtc_x.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->crtc_y.pid,
    plane->crtc_y.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->crtc_w.pid,
    plane->crtc_w.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->crtc_h.pid,
    plane->crtc_h.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->src_x.pid,
    plane->src_x.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->src_y.pid,
    plane->src_y.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->src_w.pid,
    plane->src_w.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->src_h.pid,
    plane->src_h.value));
  CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->in_fence_fd.pid,
    plane->in_fence_fd.value));
  if (plane->rotation.pid)
    CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->rotation.pid,
    plane->rotation.value));
  if (plane->ctm.pid)
    CHECK_RESULT(
    drm->ModeAtomicAddProperty(pset, id, plane->ctm.pid, plane->ctm.value));
  if (plane->alpha.pid) {
    CHECK_RESULT(drm->ModeAtomicAddProperty(pset, id, plane->alpha.pid,
      plane->alpha.value));
  }

  return 0;
}

static int remove_plane_fb(const Context *ctx,
                           Plane *plane) {
  if (plane->bo && plane->fb_id.value) {
    CHECK_RESULT(drm->ModeRmFB(ctx->fd, plane->fb_id.value));
    gbm->bo_destroy(plane->bo);
    plane->bo = nullptr;
    plane->fb_id.value = 0;
  }

  return 0;
}

static int add_plane_fb(const Context *ctx,
                        Plane *plane) {
  if (plane->format_idx < plane->drm_plane.count_formats) {
    CHECK_RESULT(remove_plane_fb(ctx, plane));
    uint32_t flags = (plane->type.value == DRM_PLANE_TYPE_CURSOR)
                       ? GBM_BO_USE_CURSOR
                       : GBM_BO_USE_SCANOUT;
    flags |= GBM_BO_USE_LINEAR;
    if (ctx->modifier != DRM_FORMAT_MOD_INVALID) {
      plane->bo = gbm->bo_create_with_modifiers(
        gbm_device, plane->crtc_w.value, plane->crtc_h.value,
        plane->drm_plane.formats[plane->format_idx], &(ctx->modifier), 1);
    } else {
      plane->bo =
          gbm->bo_create(gbm_device, plane->crtc_w.value, plane->crtc_h.value,
                         plane->drm_plane.formats[plane->format_idx], flags);
    }

    // bo creation can fail if the drm plane supports a format, but that
    // format is not supported in minigbm.
    if (!plane->bo)
      return -1;

    plane->fb_id.value = bs_drm_fb_create_gbm(plane->bo);
    CHECK(plane->fb_id.value);
    CHECK_RESULT(set_plane_props(plane, ctx->pset));
  }

  return 0;
}

static int init_plane(const Context *ctx,
                      Plane *plane,
                      const uint32_t format,
                      const uint32_t x,
                      const uint32_t y,
                      const uint32_t w,
                      const uint32_t h,
                      const uint32_t crtc_id) {
  const int32_t idx = get_format_idx(plane, format);
  if (idx < 0)
    return -EINVAL;

  plane->format_idx = idx;
  plane->crtc_x.value = x;
  plane->crtc_y.value = y;
  plane->crtc_w.value = w;
  plane->crtc_h.value = h;
  plane->src_w.value = plane->crtc_w.value << 16;
  plane->src_h.value = plane->crtc_h.value << 16;
  plane->crtc_id.value = crtc_id;
  plane->rotation.value = DRM_MODE_ROTATE_0;

  return add_plane_fb(ctx, plane);
}

static int init_plane_any_format(const Context *ctx,
                                 Plane *plane,
                                 const uint32_t x,
                                 const uint32_t y,
                                 const uint32_t w,
                                 const uint32_t h,
                                 const uint32_t crtc_id,
                                 const bool yuv) {
  if (yuv) {
    for (const unsigned int yuv_format: yuv_formats)
      if (!init_plane(ctx, plane, yuv_format, x, y, w, h, crtc_id))
        return 0;
  } else {
    // XRGB888 works well with our draw code, so try that first.
    if (!init_plane(ctx, plane, DRM_FORMAT_XRGB8888, x, y, w, h, crtc_id))
      return 0;

    for (uint32_t format_idx = 0; format_idx < plane->drm_plane.count_formats;
         format_idx++) {
      if (!gbm->device_is_format_supported(
        gbm_device, plane->drm_plane.formats[format_idx],
        GBM_BO_USE_SCANOUT | GBM_BO_USE_LINEAR))
        continue;

      if (!init_plane(ctx, plane, plane->drm_plane.formats[format_idx], x, y, w,
                      h, crtc_id))
        return 0;
    }
  }

  return -EINVAL;
}

static int disable_plane(const Context *ctx,
                         Plane *plane) {
  plane->format_idx = 0;
  plane->crtc_x.value = 0;
  plane->crtc_y.value = 0;
  plane->crtc_w.value = 0;
  plane->crtc_h.value = 0;
  plane->src_w.value = 0;
  plane->src_h.value = 0;
  plane->crtc_id.value = 0;
  plane->in_fence_fd.value = -1;

  if (plane->rotation.pid)
    plane->rotation.value = DRM_MODE_ROTATE_0;
  if (plane->ctm.pid)
    plane->ctm.value = 0;

  CHECK_RESULT(remove_plane_fb(ctx, plane));
  CHECK_RESULT(set_plane_props(plane, ctx->pset));
  return 0;
}

static int move_plane(const Context *ctx,
                      const Crtc *crtc,
                      const Plane *plane,
                      const uint32_t dx,
                      const uint32_t dy) {
  if (plane->crtc_x.value < (crtc->width - plane->crtc_w.value) &&
      plane->crtc_y.value < (crtc->height - plane->crtc_h.value)) {
    plane->crtc_x.value += dx;
    plane->crtc_y.value += dy;
    CHECK_RESULT(set_plane_props(plane, ctx->pset));
    return 0;
  }

  return -1;
}

static int scale_plane(const Context *ctx,
                       const Crtc *crtc,
                       const Plane *plane,
                       const float dw,
                       const float dh) {
  const int32_t plane_w =
      static_cast<int32_t>(plane->crtc_w.value) + static_cast<int32_t>(dw * static_cast<float>(plane->crtc_w.value));
  if (const int32_t plane_h =
        static_cast<int32_t>(plane->crtc_h.value) + static_cast<int32_t>(dh * static_cast<float>(plane->crtc_h.value));
    plane_w > 0 && plane_h > 0 &&
    (plane->crtc_x.value + plane_w < crtc->width) &&
    (plane->crtc_h.value + plane_h < crtc->height)) {
    plane->crtc_w.value = BS_ALIGN(static_cast<uint32_t>(plane_w), 2);
    plane->crtc_h.value = BS_ALIGN(static_cast<uint32_t>(plane_h), 2);
    CHECK_RESULT(set_plane_props(plane, ctx->pset));
    return 0;
  }

  return -1;
}

static void log(const Context *ctx) {
  printf("Committing the following configuration: \n");
  for (uint32_t i = 0; i < ctx->num_crtcs; i++) {
    const Crtc *crtc = &ctx->crtcs[i];
    const uint32_t num_planes =
        crtc->num_primary + crtc->num_cursor + crtc->num_overlay;
    if (!crtc->active.value)
      continue;

    printf("----- [CRTC: %u] -----\n", crtc->crtc_id);
    for (uint32_t j = 0; j < num_planes; j++) {
      if (const Plane *plane = &crtc->planes[j];
        plane->crtc_id.value == crtc->crtc_id && plane->fb_id.value) {
        uint32_t format = gbm->bo_get_format(plane->bo);
        const auto fourcc = reinterpret_cast<char *>(&format);
        printf("\t{Plane ID: %u, ", plane->drm_plane.plane_id);
        printf("Plane format: %c%c%c%c, ", fourcc[0], fourcc[1], fourcc[2],
               fourcc[3]);
        printf("Plane type: ");
        switch (plane->type.value) {
          case DRM_PLANE_TYPE_OVERLAY:
            printf("overlay, ");
            break;
          case DRM_PLANE_TYPE_CURSOR:
            printf("cursor, ");
            break;
          default:
          case DRM_PLANE_TYPE_PRIMARY:
            printf("primary, ");
            break;
        }

        printf("CRTC_X: %" PRIu64 ", CRTC_Y: %" PRIu64 ", CRTC_W: %" PRIu64
               ", CRTC_H: %" PRIu64 "}\n",
               plane->crtc_x.value, plane->crtc_y.value, plane->crtc_w.value,
               plane->crtc_h.value);
      }
    }
  }
}

static int test_commit(const Context *ctx) {
  return drm->ModeAtomicCommit(
    ctx->fd, ctx->pset,
    DRM_MODE_ATOMIC_ALLOW_MODESET | DRM_MODE_ATOMIC_TEST_ONLY, nullptr);
}

static int commit(Context *ctx) {
  fd_set fds;
  FD_ZERO(&fds);
  FD_SET(ctx->fd, &fds);

  log(ctx);
  int ret = drm->ModeAtomicCommit(
    ctx->fd, ctx->pset,
    DRM_MODE_PAGE_FLIP_EVENT | DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
  CHECK_RESULT(ret);
  do {
    ret = select(ctx->fd + 1, &fds, nullptr, nullptr, nullptr);
  } while (ret == -1 && errno == EINTR);

  CHECK_RESULT(ret);
  if (FD_ISSET(ctx->fd, &fds))
    drm->HandleEvent(ctx->fd, &ctx->drm_event_ctx);

  return 0;
}

static int test_and_commit(Context *ctx, uint32_t sleep_micro_secs) {
  sleep_micro_secs = automatic ? 0 : sleep_micro_secs;
  if (!test_commit(ctx)) {
    CHECK_RESULT(commit(ctx));
    usleep(sleep_micro_secs);
  } else {
    return TEST_COMMIT_FAIL;
  }

  return 0;
}

static int pageflip_formats(Context *ctx,
                            const Crtc *crtc,
                            Plane *plane) {
  int ret = 0;
  for (uint32_t i = 0; i < plane->drm_plane.count_formats; i++) {
    uint32_t flags = (plane->type.value == DRM_PLANE_TYPE_CURSOR)
                       ? GBM_BO_USE_CURSOR
                       : GBM_BO_USE_SCANOUT;

    flags |= GBM_BO_USE_LINEAR;
    if (!gbm->device_is_format_supported(gbm_device,
                                         plane->drm_plane.formats[i], flags))
      continue;

    CHECK_RESULT(init_plane(ctx, plane, plane->drm_plane.formats[i], 0, 0,
      crtc->width, crtc->height, crtc->crtc_id));
    CHECK_RESULT(draw_to_plane(ctx->mapper, plane, DRAW_ELLIPSE));
    ret |= test_and_commit(ctx, 1e6);

    // disable, but don't commit, since we can't have an active CRTC without any
    // planes.
    CHECK_RESULT(disable_plane(ctx, plane));
  }

  return ret;
}

static uint32_t get_connection(Crtc * /* crtc */,
                               const uint32_t crtc_index) {
  uint32_t connector_id = 0;
  const uint32_t crtc_mask = 1u << crtc_index;
  bs_drm_pipe pipe{};
  bs_drm_pipe_plumber *plumber = bs_drm_pipe_plumber_new();
  bs_drm_pipe_plumber_crtc_mask(plumber, crtc_mask);
  if (bs_drm_pipe_plumber_make(plumber, &pipe))
    connector_id = pipe.connector_id;

  bs_drm_pipe_plumber_destroy(&plumber);
  return connector_id;
}

static int enable_crtc(const Context *ctx, Crtc *crtc) {
  drm->ModeAtomicSetCursor(ctx->pset, 0);

  for (uint32_t i = 0; i < ctx->num_connectors; i++) {
    ctx->connectors[i].crtc_id.value = 0;
    set_connector_props(&ctx->connectors[i], ctx->pset);
  }

  for (uint32_t i = 0; i < ctx->num_crtcs; i++) {
    if (&ctx->crtcs[i] == crtc) {
      const uint32_t connector_id = get_connection(crtc, i);
      CHECK(connector_id);
      for (uint32_t j = 0; j < ctx->num_connectors; j++) {
        if (connector_id == ctx->connectors[j].connector_id) {
          ctx->connectors[j].crtc_id.value = crtc->crtc_id;
          set_connector_props(&ctx->connectors[j], ctx->pset);
          break;
        }
      }

      break;
    }
  }

  int ret = -EINVAL;
  const int cursor = drm->ModeAtomicGetCursor(ctx->pset);

  for (uint32_t i = 0; i < ctx->num_modes; i++) {
    const Mode *mode = &ctx->modes[i];
    drm->ModeAtomicSetCursor(ctx->pset, cursor);

    crtc->mode_id.value = mode->id;
    crtc->active.value = 1;
    crtc->width = mode->width;
    crtc->height = mode->height;

    set_crtc_props(crtc, ctx->pset);
    ret = drm->ModeAtomicCommit(
      ctx->fd, ctx->pset,
      DRM_MODE_ATOMIC_TEST_ONLY | DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
    if (!ret)
      return 0;
  }

  bs_debug_error("[CRTC:%d]: failed to find mode: %s", crtc->crtc_id,
                 strerror(errno));
  return ret;
}

static int disable_crtcs(const Context *ctx,
                         const uint32_t crtc_mask) {
  for (uint32_t i = 0; i < ctx->num_connectors; i++) {
    ctx->connectors[i].crtc_id.value = 0;
    set_connector_props(&ctx->connectors[i], ctx->pset);
  }

  for (uint32_t crtc_index = 0; crtc_index < ctx->num_crtcs; crtc_index++) {
    if (!((1 << crtc_index) & crtc_mask))
      continue;

    Crtc *crtc = &ctx->crtcs[crtc_index];
    crtc->mode_id.value = 0;
    crtc->active.value = 0;
    if (crtc->ctm.pid)
      crtc->ctm.value = 0;
    if (crtc->gamma_lut.pid)
      crtc->gamma_lut.value = 0;
    if (crtc->background_color.pid)
      crtc->background_color.value = 0;

    set_crtc_props(crtc, ctx->pset);
  }

  const int ret = drmModeAtomicCommit(ctx->fd, ctx->pset,
                                      DRM_MODE_ATOMIC_ALLOW_MODESET, nullptr);
  CHECK_RESULT(ret);
  return ret;
}

static int disable_crtc(const Context *ctx,
                        const Crtc *crtc) {
  for (uint32_t i = 0; i < ctx->num_crtcs; i++)
    if (&ctx->crtcs[i] == crtc)
      return disable_crtcs(ctx, 1 << i);
  CHECK(false); // We shouldn't get here.
}

static Context *new_context(const uint32_t num_connectors,
                            const uint32_t num_crtcs,
                            const uint32_t num_planes) {
  auto *ctx =
      static_cast<Context *>(calloc(1, sizeof(Context)));

  ctx->mapper = bs_mapper_gem_new();
  if (ctx->mapper == nullptr) {
    bs_debug_error("failed to create mapper object");
    free(ctx);
    return nullptr;
  }

  ctx->connectors = static_cast<Connector *>(
    calloc(num_connectors, sizeof(*ctx->connectors)));
  ctx->crtcs =
      static_cast<Crtc *>(calloc(num_crtcs, sizeof(*ctx->crtcs)));
  for (uint32_t i = 0; i < num_crtcs; i++) {
    ctx->crtcs[i].planes = static_cast<Plane *>(
      calloc(num_planes, sizeof(*ctx->crtcs[i].planes)));
    ctx->crtcs[i].overlay_idx =
        static_cast<uint32_t *>(calloc(num_planes, sizeof(uint32_t)));
    ctx->crtcs[i].primary_idx =
        static_cast<uint32_t *>(calloc(num_planes, sizeof(uint32_t)));
    ctx->crtcs[i].cursor_idx =
        static_cast<uint32_t *>(calloc(num_planes, sizeof(uint32_t)));
  }

  ctx->num_connectors = num_connectors;
  ctx->num_crtcs = num_crtcs;
  ctx->num_modes = 0;
  ctx->modes = nullptr;
  ctx->pset = drm->ModeAtomicAlloc();
  ctx->drm_event_ctx.version = DRM_EVENT_CONTEXT_VERSION;
  ctx->drm_event_ctx.page_flip_handler = page_flip_handler;

  return ctx;
}

static void free_context(Context *ctx) {
  for (uint32_t i = 0; i < ctx->num_crtcs; i++) {
    const uint32_t num_planes = ctx->crtcs[i].num_primary +
                                ctx->crtcs[i].num_cursor +
                                ctx->crtcs[i].num_overlay;

    for (uint32_t j = 0; j < num_planes; j++) {
      remove_plane_fb(ctx, &ctx->crtcs[i].planes[j]);
      free(ctx->crtcs[i].planes[j].drm_plane.formats);
    }

    free(ctx->crtcs[i].planes);
    free(ctx->crtcs[i].overlay_idx);
    free(ctx->crtcs[i].cursor_idx);
    free(ctx->crtcs[i].primary_idx);
  }

  drm->ModeAtomicFree(ctx->pset);
  drm->ModeFreeResources(ctx->res);
  free(ctx->modes);
  free(ctx->crtcs);
  free(ctx->connectors);
  bs_mapper_destroy(ctx->mapper);
  free(ctx);
}

static Context *query_kms(const int fd) {
  drmModeRes *res = drm->ModeGetResources(fd);
  if (res == nullptr) {
    bs_debug_error("failed to get drm resources");
    return nullptr;
  }

  drmModePlaneRes *plane_res = drmModeGetPlaneResources(fd);
  if (plane_res == nullptr) {
    bs_debug_error("failed to get plane resources");
    drm->ModeFreeResources(res);
    return nullptr;
  }

  Context *ctx = new_context(res->count_connectors, res->count_crtcs,
                             plane_res->count_planes);
  if (ctx == nullptr) {
    bs_debug_error("failed to allocate atomic context");
    drm->ModeFreePlaneResources(plane_res);
    drm->ModeFreeResources(res);
    return nullptr;
  }

  ctx->fd = fd;
  ctx->res = res;
  drmModeObjectPropertiesPtr props = nullptr;

  for (uint32_t conn_index = 0;
       conn_index < static_cast<uint32_t>(res->count_connectors);
       conn_index++) {
    const uint32_t conn_id = res->connectors[conn_index];
    ctx->connectors[conn_index].connector_id = conn_id;
    props =
        drm->ModeObjectGetProperties(fd, conn_id, DRM_MODE_OBJECT_CONNECTOR);
    get_connector_props(fd, &ctx->connectors[conn_index], props);

    drmModeConnector *connector = drm->ModeGetConnector(fd, conn_id);
    for (uint32_t mode_index = 0; mode_index < static_cast<uint32_t>(connector->count_modes); mode_index++) {
      auto new_modes = static_cast<Mode *>(realloc(ctx->modes, (ctx->num_modes + 1) * sizeof(*ctx->modes)));
      if (!new_modes) {
        drm->ModeFreeConnector(connector);
        drm->ModeFreeObjectProperties(props);
        free(ctx->modes);
        ctx->modes = nullptr;
        return nullptr;
      }
      ctx->modes = new_modes;
      drm->ModeCreatePropertyBlob(fd, &connector->modes[mode_index], sizeof(drmModeModeInfo),
                                  &ctx->modes[ctx->num_modes].id);
      ctx->modes[ctx->num_modes].width = connector->modes[mode_index].hdisplay;
      ctx->modes[ctx->num_modes].height = connector->modes[mode_index].vdisplay;
      ctx->num_modes++;
    }

    drm->ModeFreeConnector(connector);
    drm->ModeFreeObjectProperties(props);
    props = nullptr;
  }

  uint32_t crtc_index;
  for (crtc_index = 0; crtc_index < static_cast<uint32_t>(res->count_crtcs);
       crtc_index++) {
    ctx->crtcs[crtc_index].crtc_id = res->crtcs[crtc_index];
    props = drm->ModeObjectGetProperties(fd, res->crtcs[crtc_index],
                                         DRM_MODE_OBJECT_CRTC);
    get_crtc_props(fd, &ctx->crtcs[crtc_index], props);

    drm->ModeFreeObjectProperties(props);
    props = nullptr;
  }

  for (uint32_t plane_index = 0; plane_index < plane_res->count_planes;
       plane_index++) {
    drmModePlane *plane = drm->ModeGetPlane(fd, plane_res->planes[plane_index]);
    if (plane == nullptr) {
      bs_debug_error("failed to get plane id %u",
                     plane_res->planes[plane_index]);
      continue;
    }

    uint32_t crtc_mask = 0;

    drmModeObjectPropertiesPtr properties = drm->ModeObjectGetProperties(
      fd, plane_res->planes[plane_index], DRM_MODE_OBJECT_PLANE);

    for (crtc_index = 0; crtc_index < static_cast<uint32_t>(res->count_crtcs);
         crtc_index++) {
      crtc_mask = 1U << crtc_index;
      if (plane->possible_crtcs & crtc_mask) {
        Crtc *crtc = &ctx->crtcs[crtc_index];
        const uint32_t cursor_idx = crtc->num_cursor;
        const uint32_t primary_idx = crtc->num_primary;
        const uint32_t overlay_idx = crtc->num_overlay;
        const uint32_t idx = cursor_idx + primary_idx + overlay_idx;
        copy_drm_plane(&crtc->planes[idx].drm_plane, plane);
        get_plane_props(fd, &crtc->planes[idx], properties);
        switch (crtc->planes[idx].type.value) {
          case DRM_PLANE_TYPE_OVERLAY:
            crtc->overlay_idx[overlay_idx] = idx;
            crtc->num_overlay++;
            break;
          case DRM_PLANE_TYPE_PRIMARY:
            crtc->primary_idx[primary_idx] = idx;
            crtc->num_primary++;
            break;
          case DRM_PLANE_TYPE_CURSOR:
            crtc->cursor_idx[cursor_idx] = idx;
            crtc->num_cursor++;
            break;
          default:
            bs_debug_error("invalid plane type returned");
            return nullptr;
        }

        /*
         * The DRM UAPI states that cursor and overlay framebuffers may be
         * present after a CRTC disable, so zero this out so we can get a
         * clean slate.
         */
        crtc->planes[idx].fb_id.value = 0;
      }
    }

    drm->ModeFreePlane(plane);
    drm->ModeFreeObjectProperties(properties);
    properties = nullptr;
  }

  drm->ModeFreePlaneResources(plane_res);
  return ctx;
}

static int test_multiple_planes(Context *ctx,
                                const Crtc *crtc) {
  int ret = 0;
  Plane *cursor;
  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    bool video = true;
    uint32_t x, y;
    for (uint32_t j = 0; j < crtc->num_overlay; j++) {
      Plane *overlay = get_plane(crtc, j, DRM_PLANE_TYPE_OVERLAY);
      x = crtc->width >> (j + 2);
      y = crtc->height >> (j + 2);
      x = MAX(x, ctx->res->min_width);
      y = MAX(y, ctx->res->min_height);
      // drm->ModeAddFB2 requires the height and width are even for subsampled YUV formats.
      x = BS_ALIGN(x, 2);
      y = BS_ALIGN(y, 2);
      if (video && !init_plane_any_format(ctx, overlay, x, y, x, y,
                                          crtc->crtc_id, true)) {
        CHECK_RESULT(draw_to_plane(ctx->mapper, overlay, DRAW_STRIPE));
        video = false;
      } else {
        CHECK_RESULT(init_plane_any_format(ctx, overlay, x, y, x, y,
          crtc->crtc_id, false));
        CHECK_RESULT(draw_to_plane(ctx->mapper, overlay, DRAW_LINES));
      }
    }

    for (uint32_t j = 0; j < crtc->num_cursor; j++) {
      x = crtc->width >> (j + 2);
      y = crtc->height >> (j + 2);
      cursor = get_plane(crtc, j, DRM_PLANE_TYPE_CURSOR);
      CHECK_RESULT(init_plane(ctx, cursor, DRM_FORMAT_ARGB8888, x, y,
        CURSOR_SIZE, CURSOR_SIZE, crtc->crtc_id));
      CHECK_RESULT(draw_to_plane(ctx->mapper, cursor, DRAW_CURSOR));
    }

    Plane *primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    CHECK_RESULT(init_plane_any_format(ctx, primary, 0, 0, crtc->width,
      crtc->height, crtc->crtc_id, false));
    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_ELLIPSE));

    const uint32_t num_planes =
        crtc->num_primary + crtc->num_cursor + crtc->num_overlay;
    int done = 0;
    while (!done) {
      done = 1;
      for (uint32_t j = 0; j < num_planes; j++) {
        if (const Plane *plane = &crtc->planes[j];
          plane->type.value != DRM_PLANE_TYPE_PRIMARY)
          done &= move_plane(ctx, crtc, plane, 40, 40);
      }

      ret |= test_and_commit(ctx, static_cast<uint32_t>(1e6 / 60));
    }

    ret |= test_and_commit(ctx, 1e6);

    /* Disable primary plane and verify overlays show up. */
    CHECK_RESULT(disable_plane(ctx, primary));
    ret |= test_and_commit(ctx, 1e6);

    for (uint32_t j = 0; j < crtc->num_cursor; j++) {
      cursor = get_plane(crtc, j, DRM_PLANE_TYPE_CURSOR);
      CHECK_RESULT(disable_plane(ctx, cursor));
    }
  }

  return ret;
}

static int test_video_overlay(Context *ctx,
                              const Crtc *crtc) {
  int ret = 0;
  uint32_t num_plane_init_fails = 0;
  for (uint32_t i = 0; i < crtc->num_overlay; i++) {
    Plane *overlay = get_plane(crtc, i, DRM_PLANE_TYPE_OVERLAY);
    if (init_plane_any_format(ctx, overlay, 0, 0, 800, 800, crtc->crtc_id,
                              true)) {
      num_plane_init_fails++;
      continue;
    }

    CHECK_RESULT(draw_to_plane(ctx->mapper, overlay, DRAW_STRIPE));
    while (!move_plane(ctx, crtc, overlay, 40, 40))
      ret |= test_and_commit(ctx, static_cast<uint32_t>(1e6 / 60));
  }

  if (num_plane_init_fails == crtc->num_overlay) {
    bs_debug_error("No planes support video formats");
    return -1;
  }
  return ret;
}

static int prop_contains_value(const drmModePropertyRes *prop, const uint64_t value) {
  for (uint32_t i = 0; i < static_cast<uint32_t>(prop->count_values); i++) {
    if (prop->values[i] == value)
      return 1;
  }
  return 0;
}

static int test_orientation(Context *ctx,
                            const Crtc *crtc) {
  int ret = 0;
  drmModePropertyPtr rotation_prop;
  for (uint32_t i = 0; i < crtc->num_overlay; i++) {
    Plane *overlay = get_plane(crtc, i, DRM_PLANE_TYPE_OVERLAY);
    if (!overlay->rotation.pid)
      continue;

    rotation_prop = drm->ModeGetProperty(ctx->fd, overlay->rotation.pid);
    CHECK(rotation_prop);
    CHECK_RESULT(init_plane_any_format(ctx, overlay, 0, 0, crtc->width,
      crtc->height, crtc->crtc_id, false));
    CHECK(prop_contains_value(rotation_prop, DRM_MODE_ROTATE_0_LOG2));
    overlay->rotation.value = DRM_MODE_ROTATE_0;
    set_plane_props(overlay, ctx->pset);
    CHECK_RESULT(draw_to_plane(ctx->mapper, overlay, DRAW_LINES));
    ret |= test_and_commit(ctx, 1e6);

    if (prop_contains_value(rotation_prop, DRM_MODE_REFLECT_Y_LOG2)) {
      overlay->rotation.value = DRM_MODE_REFLECT_Y;
      set_plane_props(overlay, ctx->pset);
      ret |= test_and_commit(ctx, 1e6);

      CHECK_RESULT(disable_plane(ctx, overlay));
    }
    drm->ModeFreeProperty(rotation_prop);
    rotation_prop = nullptr;
  }

  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    Plane *primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    if (!primary->rotation.pid)
      continue;

    rotation_prop = drm->ModeGetProperty(ctx->fd, primary->rotation.pid);
    CHECK(rotation_prop);
    CHECK_RESULT(init_plane_any_format(ctx, primary, 0, 0, crtc->width,
      crtc->height, crtc->crtc_id, false));
    CHECK(prop_contains_value(rotation_prop, DRM_MODE_ROTATE_0_LOG2));
    primary->rotation.value = DRM_MODE_ROTATE_0;
    set_plane_props(primary, ctx->pset);
    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_LINES));
    ret |= test_and_commit(ctx, 1e6);

    if (prop_contains_value(rotation_prop, DRM_MODE_REFLECT_Y_LOG2)) {
      primary->rotation.value = DRM_MODE_REFLECT_Y;
      set_plane_props(primary, ctx->pset);
      ret |= test_and_commit(ctx, 1e6);
    }

    CHECK_RESULT(disable_plane(ctx, primary));
    drm->ModeFreeProperty(rotation_prop);
    rotation_prop = nullptr;
  }

  return ret;
}

static int test_plane_alpha(Context *ctx,
                            const Crtc *crtc) {
  int ret = 0;
  const int offset_x = static_cast<int>(crtc->width / 5);
  const int offset_y = static_cast<int>(crtc->height / 5);

  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    Plane *primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    if (!primary->alpha.pid)
      return 0;

    CHECK_RESULT(init_plane_any_format(ctx, primary, 0, 0, crtc->width,
      crtc->height, crtc->crtc_id, false));
    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_ELLIPSE));
    primary->alpha.value = 0xffff;
    set_plane_props(primary, ctx->pset);
    ret |= test_and_commit(ctx, 1e6 / 2);

    for (uint32_t j = 0; j < crtc->num_overlay; j++) {
      Plane *overlay = get_plane(crtc, j, DRM_PLANE_TYPE_OVERLAY);
      if (!overlay->alpha.pid)
        return 0;

      CHECK_RESULT(init_plane_any_format(
        ctx, overlay, offset_x, offset_y, crtc->width - offset_x * 2,
        crtc->height - offset_y * 2, crtc->crtc_id, false));
      CHECK_RESULT(draw_to_plane(ctx->mapper, overlay, DRAW_STRIPE));
      ret |= test_and_commit(ctx, 1e6 / 2);

      for (uint32_t a = 0xffff; a > 0; a = a >> 1) {
        overlay->alpha.value = a;
        set_plane_props(overlay, ctx->pset);
        ret |= test_and_commit(ctx, 1e6 / 5);
      }
      overlay->alpha.value = 0xffff;
      set_plane_props(overlay, ctx->pset);
    }

    for (uint32_t a = 0xffff; a > 0; a = a >> 1) {
      primary->alpha.value = a;
      set_plane_props(primary, ctx->pset);
      ret |= test_and_commit(ctx, 1e6 / 5);
    }
  }

  return ret;
}

static void *inc_timeline(void *user_data) {
  const int timeline_fd = *static_cast<int *>(user_data);
  const uint32_t sleep_micro_secs = automatic ? 1e3 : 1e5;
  usleep(sleep_micro_secs);
  sw_sync_timeline_inc(timeline_fd, 1);
  return nullptr;
}

static int test_in_fence(Context *ctx, const Crtc *crtc) {
  int ret = 0;
  pthread_t inc_timeline_thread;
  Plane *primary = nullptr;

  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    CHECK_RESULT(init_plane_any_format(ctx, primary, 0, 0, crtc->width,
      crtc->height, crtc->crtc_id, false));
    break;
  }

  int timeline = sw_sync_timeline_create();
  CHECK(fcntl(timeline, F_GETFD, 0) >= 0);
  const int in_fence = sw_sync_fence_create(timeline, "test_in_fence", 1);
  CHECK(fcntl(in_fence, F_GETFD, 0) >= 0);

  CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_LINES));

  primary->in_fence_fd.value = in_fence;
  set_plane_props(primary, ctx->pset);
  set_crtc_props(crtc, ctx->pset);

  CHECK(
    !pthread_create(&inc_timeline_thread, nullptr, inc_timeline, &timeline));

  ret |= test_and_commit(ctx, 1e6);
  CHECK(!pthread_join(inc_timeline_thread, nullptr));

  ret |= test_and_commit(ctx, 1e6);
  close(in_fence);
  close(timeline);

  return ret;
}

static int test_out_fence(Context *ctx,
                          const Crtc *crtc) {
  int ret = 0;
  Plane *primary = nullptr;

  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    CHECK_RESULT(init_plane_any_format(ctx, primary, 0, 0, crtc->width,
      crtc->height, crtc->crtc_id, false));
    break;
  }

  CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_LINES));
  set_plane_props(primary, ctx->pset);
  int out_fence_fd = 0;
  crtc->out_fence_ptr.value = reinterpret_cast<uint64_t>(&out_fence_fd);
  set_crtc_props(crtc, ctx->pset);
  ret |= test_and_commit(ctx, 1e6);
  CHECK(out_fence_fd);
  // |out_fence_fd| will signal when the currently scanned out buffers are
  // replaced. In this case we're waiting with a timeout of 0 only to check that
  // |out_fence_fd| is a valid fence.
  CHECK(!sync_wait(out_fence_fd, 0));
  close(out_fence_fd);
  // Reset this to NULL so subsequent commits from the same atomictest_context
  // will not set the out_fence property. If not cleared, this will result in
  // stack corruption since out_fence_fd is on the stack.
  crtc->out_fence_ptr.value = 0;
  return ret;
}

static int test_plane_ctm(Context *ctx,
                          const Crtc *crtc) {
  int ret = 0;

  for (uint32_t i = 0; i < crtc->num_overlay; i++) {
    Plane *overlay = get_plane(crtc, i, DRM_PLANE_TYPE_OVERLAY);
    if (!overlay->ctm.pid)
      continue;

    CHECK_RESULT(init_plane(ctx, overlay, DRM_FORMAT_XRGB8888, 0, 0,
      crtc->width, crtc->height, crtc->crtc_id));

    CHECK_RESULT(drmModeCreatePropertyBlob64(
      ctx->fd, identity_ctm, sizeof(identity_ctm), &overlay->ctm.value));
    set_plane_props(overlay, ctx->pset);
    CHECK_RESULT(draw_to_plane(ctx->mapper, overlay, DRAW_LINES));
    ret |= test_and_commit(ctx, 1e6);
    CHECK_RESULT(drmModeDestroyPropertyBlob64(ctx->fd, overlay->ctm.value));

    CHECK_RESULT(drmModeCreatePropertyBlob64(
      ctx->fd, red_shift_ctm, sizeof(red_shift_ctm), &overlay->ctm.value));
    set_plane_props(overlay, ctx->pset);
    ret |= test_and_commit(ctx, 1e6);
    CHECK_RESULT(drmModeDestroyPropertyBlob64(ctx->fd, overlay->ctm.value));

    CHECK_RESULT(disable_plane(ctx, overlay));
  }

  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    Plane *primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    if (!primary->ctm.pid)
      continue;

    CHECK_RESULT(init_plane_any_format(ctx, primary, 0, 0, crtc->width,
      crtc->height, crtc->crtc_id, false));
    CHECK_RESULT(drmModeCreatePropertyBlob64(
      ctx->fd, identity_ctm, sizeof(identity_ctm), &primary->ctm.value));
    set_plane_props(primary, ctx->pset);
    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_LINES));
    ret |= test_and_commit(ctx, 1e6);
    CHECK_RESULT(drmModeDestroyPropertyBlob64(ctx->fd, primary->ctm.value));

    CHECK_RESULT(drmModeCreatePropertyBlob64(
      ctx->fd, red_shift_ctm, sizeof(red_shift_ctm), &primary->ctm.value));
    set_plane_props(primary, ctx->pset);
    ret |= test_and_commit(ctx, 1e6);
    CHECK_RESULT(drmModeDestroyPropertyBlob64(ctx->fd, primary->ctm.value));

    CHECK_RESULT(disable_plane(ctx, primary));
  }

  return ret;
}

static int test_video_underlay(Context *ctx,
                               const Crtc *crtc) {
  int ret = 0;
  Plane *underlay = nullptr;
  Plane *primary = nullptr;

  for (int i = 0; i < static_cast<int>(crtc->num_primary + crtc->num_overlay);
       ++i) {
    if (crtc->planes[i].type.value != DRM_PLANE_TYPE_CURSOR) {
      if (!underlay) {
        underlay = &crtc->planes[i];
      } else {
        primary = &crtc->planes[i];
        break;
      }
    }
  }
  if (!underlay || !primary)
    return 0;

  if (init_plane_any_format(ctx, underlay, 0, 0, crtc->width >> 2,
                            crtc->height >> 2, crtc->crtc_id, true)) {
    // Fall back to a non YUV format.
    CHECK_RESULT(init_plane_any_format(ctx, underlay, 0, 0, crtc->width >> 2,
      crtc->height >> 2, crtc->crtc_id,
      false));
  }

  CHECK_RESULT(draw_to_plane(ctx->mapper, underlay, DRAW_LINES));

  CHECK_RESULT(init_plane(ctx, primary, DRM_FORMAT_ARGB8888, 0, 0, crtc->width,
    crtc->height, crtc->crtc_id));
  CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_TRANSPARENT_HOLE));

  while (!move_plane(ctx, crtc, underlay, 50, 20))
    ret |= test_and_commit(ctx, static_cast<uint32_t>(1e6 / 60));

  return ret;
}

static int test_fullscreen_video(Context *ctx,
                                 const Crtc *crtc) {
  int ret = 0;
  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    Plane *primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    if (init_plane_any_format(ctx, primary, 0, 0, crtc->width, crtc->height,
                              crtc->crtc_id, true))
      continue;

    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_STRIPE));
    ret |= test_and_commit(ctx, 1e6);
  }

  return ret;
}

static int test_disable_primary(Context *ctx,
                                const Crtc *crtc) {
  int ret = 0;
  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    for (uint32_t j = 0; j < crtc->num_overlay; j++) {
      Plane *overlay = get_plane(crtc, j, DRM_PLANE_TYPE_OVERLAY);
      uint32_t x = crtc->width >> (j + 2);
      uint32_t y = crtc->height >> (j + 2);
      x = MAX(x, ctx->res->min_width);
      y = MAX(y, ctx->res->min_height);
      CHECK_RESULT(init_plane_any_format(ctx, overlay, x, y, x, y,
        crtc->crtc_id, false));
      CHECK_RESULT(draw_to_plane(ctx->mapper, overlay, DRAW_LINES));
    }

    Plane *primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    CHECK_RESULT(init_plane_any_format(ctx, primary, 0, 0, crtc->width,
      crtc->height, crtc->crtc_id, false));
    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_ELLIPSE));
    ret |= test_and_commit(ctx, 1e6);

    /* Disable primary plane. */
    disable_plane(ctx, primary);
    ret |= test_and_commit(ctx, 1e6);
  }

  return ret;
}

static int test_rgba_primary(Context *ctx,
                             const Crtc *crtc) {
  int ret = 0;
  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    Plane *primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    bool has_argb = false;
    for (uint32_t j = 0; j < primary->drm_plane.count_formats; ++j) {
      if (primary->drm_plane.formats[j] == DRM_FORMAT_ARGB8888) {
        has_argb = true;
        break;
      }
    }
    if (!has_argb)
      return 0;

    CHECK_RESULT(init_plane(ctx, primary, DRM_FORMAT_ARGB8888, 0, 0,
      crtc->width, crtc->height, crtc->crtc_id));

    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_LINES));

    ret |= test_and_commit(ctx, 1e6);
  }

  return ret;
}

static int test_overlay_pageflip(Context *ctx,
                                 const Crtc *crtc) {
  for (uint32_t i = 0; i < crtc->num_overlay; i++) {
    Plane *overlay = get_plane(crtc, i, DRM_PLANE_TYPE_OVERLAY);
    CHECK_RESULT(pageflip_formats(ctx, crtc, overlay));
  }

  return 0;
}

static int test_overlay_downscaling(Context *ctx,
                                    const Crtc *crtc) {
  int ret = 0;
  const uint32_t w = BS_ALIGN(crtc->width / 2, 2);
  const uint32_t h = BS_ALIGN(crtc->height / 2, 2);
  for (uint32_t i = 0; i < crtc->num_overlay; i++) {
    Plane *overlay = get_plane(crtc, i, DRM_PLANE_TYPE_OVERLAY);
    if (init_plane_any_format(ctx, overlay, 0, 0, w, h, crtc->crtc_id, true))
      CHECK_RESULT(init_plane(ctx, overlay, DRM_FORMAT_XRGB8888, 0, 0, w, h,
      crtc->crtc_id));
    CHECK_RESULT(draw_to_plane(ctx->mapper, overlay, DRAW_LINES));
    ret |= test_and_commit(ctx, 1e6);

    while (!scale_plane(ctx, crtc, overlay, -.1f, -.1f) && !test_commit(ctx)) {
      CHECK_RESULT(commit(ctx));
      usleep(1e6);
    }

    disable_plane(ctx, overlay);
  }

  return ret;
}

static int test_overlay_upscaling(Context *ctx,
                                  const Crtc *crtc) {
  int ret = 0;
  const uint32_t w = BS_ALIGN(crtc->width / 4, 2);
  const uint32_t h = BS_ALIGN(crtc->height / 4, 2);
  for (uint32_t i = 0; i < crtc->num_overlay; i++) {
    Plane *overlay = get_plane(crtc, i, DRM_PLANE_TYPE_OVERLAY);
    if (init_plane_any_format(ctx, overlay, 0, 0, w, h, crtc->crtc_id, true))
      CHECK_RESULT(init_plane(ctx, overlay, DRM_FORMAT_XRGB8888, 0, 0, w, h,
      crtc->crtc_id));
    CHECK_RESULT(draw_to_plane(ctx->mapper, overlay, DRAW_LINES));
    ret |= test_and_commit(ctx, 1e6);

    while (!scale_plane(ctx, crtc, overlay, .1f, .1f) && !test_commit(ctx)) {
      CHECK_RESULT(commit(ctx));
      usleep(1e6);
    }

    disable_plane(ctx, overlay);
  }

  return ret;
}

static int test_primary_pageflip(Context *ctx,
                                 const Crtc *crtc) {
  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    Plane *primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    CHECK_RESULT(pageflip_formats(ctx, crtc, primary));
  }

  return 0;
}

static int test_crtc_ctm(Context *ctx, const Crtc *crtc) {
  int ret = 0;
  Plane *primary;
  if (!crtc->ctm.pid)
    return 0;

  CHECK_RESULT(drmModeCreatePropertyBlob64(
    ctx->fd, identity_ctm, sizeof(identity_ctm), &crtc->ctm.value));
  set_crtc_props(crtc, ctx->pset);
  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);

    CHECK_RESULT(init_plane(ctx, primary, DRM_FORMAT_XRGB8888, 0, 0,
      crtc->width, crtc->height, crtc->crtc_id));
    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_LINES));
    ret |= test_and_commit(ctx, 1e6);

    primary->crtc_id.value = 0;
    CHECK_RESULT(set_plane_props(primary, ctx->pset));
  }

  CHECK_RESULT(drmModeDestroyPropertyBlob64(ctx->fd, crtc->ctm.value));

  CHECK_RESULT(drmModeCreatePropertyBlob64(
    ctx->fd, red_shift_ctm, sizeof(red_shift_ctm), &crtc->ctm.value));
  set_crtc_props(crtc, ctx->pset);
  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);
    primary->crtc_id.value = crtc->crtc_id;
    CHECK_RESULT(set_plane_props(primary, ctx->pset));

    ret |= test_and_commit(ctx, 1e6);

    primary->crtc_id.value = 0;
    CHECK_RESULT(disable_plane(ctx, primary));
  }

  CHECK_RESULT(drmModeDestroyPropertyBlob64(ctx->fd, crtc->ctm.value));

  return ret;
}

static void gamma_linear(drm_color_lut *table, const int size) {
  for (int i = 0; i < size; i++) {
    float v = static_cast<float>(i) / static_cast<float>(size - 1);
    v *= static_cast<float>(GAMMA_MAX_VALUE);
    table[i].red = static_cast<uint16_t>(v);
    table[i].green = static_cast<uint16_t>(v);
    table[i].blue = static_cast<uint16_t>(v);
  }
}

static void gamma_step(drm_color_lut *table, const int size) {
  for (int i = 0; i < size; i++) {
    const float v = i < size / 2 ? 0 : GAMMA_MAX_VALUE;
    table[i].red = static_cast<uint16_t>(v);
    table[i].green = static_cast<uint16_t>(v);
    table[i].blue = static_cast<uint16_t>(v);
  }
}

static int test_crtc_gamma(Context *ctx,
                           const Crtc *crtc) {
  int ret = 0;
  Plane *primary;
  if (!crtc->gamma_lut.pid || !crtc->gamma_lut_size.pid)
    return 0;

  if (crtc->gamma_lut_size.value == 0)
    return 0;

  auto *gamma_table = static_cast<drm_color_lut *>(
    calloc(crtc->gamma_lut_size.value, sizeof(drm_color_lut)));

  gamma_linear(gamma_table, static_cast<int>(crtc->gamma_lut_size.value));
  CHECK_RESULT(drmModeCreatePropertyBlob64(
    ctx->fd, gamma_table,
    sizeof(struct drm_color_lut) * crtc->gamma_lut_size.value,
    &crtc->gamma_lut.value));
  set_crtc_props(crtc, ctx->pset);

  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);

    CHECK_RESULT(init_plane(ctx, primary, DRM_FORMAT_XRGB8888, 0, 0,
      crtc->width, crtc->height, crtc->crtc_id));

    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_STRIPE));
    ret |= test_and_commit(ctx, 1e6);

    CHECK_RESULT(disable_plane(ctx, primary));
  }

  CHECK_RESULT(drm->ModeDestroyPropertyBlob(ctx->fd, crtc->gamma_lut.value));

  gamma_step(gamma_table, static_cast<int>(crtc->gamma_lut_size.value));
  CHECK_RESULT(drmModeCreatePropertyBlob64(
    ctx->fd, gamma_table,
    sizeof(struct drm_color_lut) * crtc->gamma_lut_size.value,
    &crtc->gamma_lut.value));
  set_crtc_props(crtc, ctx->pset);

  for (uint32_t i = 0; i < crtc->num_primary; i++) {
    primary = get_plane(crtc, i, DRM_PLANE_TYPE_PRIMARY);

    CHECK_RESULT(init_plane(ctx, primary, DRM_FORMAT_XRGB8888, 0, 0,
      crtc->width, crtc->height, crtc->crtc_id));

    CHECK_RESULT(draw_to_plane(ctx->mapper, primary, DRAW_STRIPE));
    ret |= test_and_commit(ctx, 1e6);

    CHECK_RESULT(disable_plane(ctx, primary));
  }

  CHECK_RESULT(drm->ModeDestroyPropertyBlob(ctx->fd, crtc->gamma_lut.value));
  free(gamma_table);

  return ret;
}

static uint64_t pack_rgba_64(const uint64_t red,
                             const uint64_t green,
                             const uint64_t blue,
                             const uint64_t alpha) {
  return alpha << 48 | blue << 32 | green << 16 | red;
}

static int test_crtc_background_color(Context *ctx,
                                      const Crtc *crtc) {
  int ret = 0;

  if (!crtc->background_color.pid)
    return 0;

  struct crtc_background_color {
    const char *name;
    uint64_t rgba_value;
  };

  const crtc_background_color colors[] = {
    {"black", pack_rgba_64(0, 0, 0, 0xffff)},
    {"red", pack_rgba_64(0xffff, 0, 0, 0xffff)},
    {"green", pack_rgba_64(0, 0xffff, 0, 0xffff)},
    {"blue", pack_rgba_64(0, 0, 0xffff, 0xffff)},
  };

  for (auto [name, rgba_value]: colors) {
    crtc->background_color.value = rgba_value;
    set_crtc_props(crtc, ctx->pset);
    ret |= test_and_commit(ctx, 1e6);
  }

  return ret;
}

static constexpr Testcase cases[] = {
  {"disable_primary", test_disable_primary},
  {"rgba_primary", test_rgba_primary},
  {"fullscreen_video", test_fullscreen_video},
  {"multiple_planes", test_multiple_planes},
  {"overlay_pageflip", test_overlay_pageflip},
  {"overlay_downscaling", test_overlay_downscaling},
  {"overlay_upscaling", test_overlay_upscaling},
  {"primary_pageflip", test_primary_pageflip},
  {"video_overlay", test_video_overlay},
  {"orientation", test_orientation},
  {"video_underlay", test_video_underlay},
  {"in_fence", test_in_fence},
  {"out_fence", test_out_fence},
  /* CTM stands for Color Transform Matrix. */
  {"plane_ctm", test_plane_ctm},
  {"plane_alpha", test_plane_alpha},
  {"crtc_ctm", test_crtc_ctm},
  {"crtc_gamma", test_crtc_gamma},
  {"crtc_background_color", test_crtc_background_color},
};

static int run_testcase(Context *ctx,
                        const Crtc *crtc,
                        const test_function func) {
  const int cursor = drm->ModeAtomicGetCursor(ctx->pset);
  const uint32_t num_planes =
      crtc->num_primary + crtc->num_cursor + crtc->num_overlay;

  const int ret = func(ctx, crtc);

  for (uint32_t i = 0; i < num_planes; i++)
    disable_plane(ctx, &crtc->planes[i]);

  drm->ModeAtomicSetCursor(ctx->pset, cursor);

  CHECK_RESULT(commit(ctx));
  usleep(static_cast<__useconds_t>(1e6 / 60));

  return ret;
}

static int run_atomictest(const char * /* name */,
                          const uint32_t crtc_mask,
                          const uint64_t modifier) {
  int ret = 0;
  uint32_t num_run = 0;
  const int fd = bs_drm_open_main_display();
  CHECK_RESULT(fd);

  gbm_device = gbm->create_device(fd);
  if (!gbm_device) {
    bs_debug_error("failed to create gbm device");
    ret = -1;
    close(fd);
    return ret;
  }

  ret = drm->SetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (ret) {
    bs_debug_error("failed to enable DRM_CLIENT_CAP_UNIVERSAL_PLANES");
    ret = -1;
    gbm->device_destroy(gbm_device);
    close(fd);
    return ret;
  }

  ret = drm->SetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
  if (ret) {
    bs_debug_warning("failed to enable DRM_CLIENT_CAP_ATOMIC");
    /* We want to allow per-board disabling of atomic */
    ret = 0;
    gbm->device_destroy(gbm_device);
    close(fd);
    return ret;
  }

  auto *ctx = query_kms(fd);
  if (!ctx) {
    bs_debug_error("querying atomictest failed.");
    ret = -1;
    gbm_device_destroy(gbm_device);
    close(fd);
    return ret;
  }

  ctx->modifier = modifier;
  /* Disable all CRTCs first, in case there are any dangling connections. */
  disable_crtcs(ctx, ~0);
  for (uint32_t crtc_index = 0; crtc_index < ctx->num_crtcs; crtc_index++) {
    Crtc *crtc = &ctx->crtcs[crtc_index];
    if (!((1 << crtc_index) & crtc_mask))
      continue;

    for (auto [name, test_func]: cases) {
      if (strcmp(name, name) != 0 && strcmp("all", name) != 0)
        continue;

      num_run++;
      ret = enable_crtc(ctx, crtc);
      if (ret)
        continue;

      ret = run_testcase(ctx, crtc, test_func);
      if (ret < 0)
        goto out;

      if (ret == TEST_COMMIT_FAIL)
        bs_debug_warning("%s failed test commit, testcase not run.", name);

      ret = disable_crtc(ctx, crtc);
      if (ret)
        goto out;
    }
  }

  ret = (num_run == 0);

out:
  free_context(ctx);
  gbm->device_destroy(gbm_device);
  close(fd);

  return ret;
}

static constexpr option longopts[] = {
  {"crtc", required_argument, nullptr, 'c'},
  {"test_name", required_argument, nullptr, 't'},
  {"help", no_argument, nullptr, 'h'},
  {"automatic", no_argument, nullptr, 'a'},
  {"modifier", required_argument, nullptr, 'm'},
  {nullptr, 0, nullptr, 0},
};

static void print_help(const char *argv0) {
  printf("usage: %s [OPTIONS]\n", argv0);
  printf("  -t, --test_name <test_name>  name of test to run.\n");
  printf("  -c, --crtc <crtc_index>      index of crtc to run against.\n");
  printf("  -a, --automatic              don't sleep between tests.\n");
  printf("  -m, --modifier <modifier>    pass modifiers.\n");
  printf(" <test_name> is one the following:\n");
  for (auto i: cases)
    printf("  %s\n", i.name);
  printf("  all\n");

  if (const int fd = bs_drm_open_main_display()) {
    printf(" <modifier> must be one of ");
    bs_print_supported_modifiers(fd);
    close(fd);
  } else {
    printf("unable to show supported modifiers\n");
  }
}

int main(const int argc, char **argv) {
  int c;
  char *name = nullptr;
  int32_t crtc_idx = -1;
  uint32_t crtc_mask = ~0;
  uint64_t modifier = DRM_FORMAT_MOD_INVALID;

  while ((c = getopt_long(argc, argv, "c:t:h:am:", longopts, nullptr)) != -1) {
    switch (c) {
      case 'a':
        automatic = true;
        break;
      case 't':
        if (name) {
          free(name);
          name = nullptr;
        }
        name = strdup(optarg);
        break;
      case 'm':
        modifier = bs_string_to_modifier(optarg);
        if (modifier == UINT64_MAX) {
          bs_debug_error("unsupported modifier: %s", optarg);
          print_help(argv[0]);
          return 0;
        }
        break;
      default:
      case 'h':
        print_help(argv[0]);
        return 0;
      case 'c':
        char *endptr;
        errno = 0;
        const long val = strtol(optarg, &endptr, 10);
        if (errno != 0 || *endptr != '\0' || val < INT32_MIN || val > INT32_MAX) {
          print_help(argv[0]);
          return 0;
        }
        crtc_idx = static_cast<int32_t>(val);
        break;
    }
  }

  if (!name) {
    print_help(argv[0]);
    return 0;
  }

  if (crtc_idx >= 0) {
    crtc_mask = 1 << crtc_idx;
  }

  const int ret = run_atomictest(name, crtc_mask, modifier);
  if (ret == 0) {
    printf("[  PASSED  ] atomictest.%s\n", name);
  } else if (ret < 0) {
    printf("[  FAILED  ] atomictest.%s\n", name);
  }

  free(name);

  if (ret > 0) {
    print_help(argv[0]);
    return 0;
  }

  return ret;
}
