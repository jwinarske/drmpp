/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright 2015 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

extern "C" {
#include "bs_drm.h"
}

#include <getopt.h>
#include <cmath>

#define TABLE_LINEAR 0
#define TABLE_NEGATIVE 1
#define TABLE_POW 2
#define TABLE_STEP 3
// TABLE_PIECEWISE_HDR mimics Chrome's PIECEWISE_HDR gfx::ColorSpace::TranferID,
// basically an sRGB up to a given elbow or joint, and linear after.
#define TABLE_PIECEWISE_HDR 4

#define FLAG_INTERNAL 'i'
#define FLAG_EXTERNAL 'e'
#define FLAG_GAMMA 'g'
#define FLAG_LINEAR 'l'
#define FLAG_NEGATIVE 'n'
#define FLAG_TIME 't'
#define FLAG_CRTCS 'c'
#define FLAG_PERSIST 'p'
#define FLAG_STEP 's'
#define FLAG_HDR 'x'
#define FLAG_HELP 'h'

static option command_options[] = {
  {"internal", no_argument, nullptr, FLAG_INTERNAL},
  {"external", no_argument, nullptr, FLAG_EXTERNAL},
  {"gamma", required_argument, nullptr, FLAG_GAMMA},
  {"linear", no_argument, nullptr, FLAG_LINEAR},
  {"negative", no_argument, nullptr, FLAG_NEGATIVE},
  {"time", required_argument, nullptr, FLAG_TIME},
  {"crtcs", required_argument, nullptr, FLAG_CRTCS},
  {"persist", no_argument, nullptr, FLAG_PERSIST},
  {"step", no_argument, nullptr, FLAG_STEP},
  {"hdr", required_argument, nullptr, FLAG_HDR},
  {"help", no_argument, nullptr, FLAG_HELP},
  {nullptr, 0, nullptr, 0}
};

static void gamma_linear(uint16_t *table, const int size) {
  if (table == nullptr)
    return;
  for (int i = 0; i < size; i++) {
    float v = static_cast<float>(i) / static_cast<float>(size - 1);
    v *= 65535.0f;
    table[i] = static_cast<uint16_t>(v);
  }
}

static void gamma_inv(uint16_t *table, const int size) {
  if (table == nullptr)
    return;
  for (int i = 0; i < size; i++) {
    float v = static_cast<float>(size - 1 - i) / static_cast<float>(size - 1);
    v *= 65535.0f;
    table[i] = static_cast<uint16_t>(v);
  }
}

static void gamma_pow(uint16_t *table, const int size, float p) {
  if (table == nullptr)
    return;
  for (int i = 0; i < size; i++) {
    float v = static_cast<float>(i) / static_cast<float>(size - 1);
    v = powf(v, p);
    v *= 65535.0f;
    table[i] = static_cast<uint16_t>(v);
  }
}

static void gamma_step(uint16_t *table, const int size) {
  if (table == nullptr)
    return;
  for (int i = 0; i < size; i++) {
    table[i] = (i < size / 2) ? 0 : 65535;
  }
}

static void gamma_piecewise_linear(uint16_t *table, const int size, const float joint) {
  if (table == nullptr)
    return;

  constexpr float gamma = 1 / 2.2f;
  // |joint_i| is the index in [0, size) where sRGB and linear curves meet.
  const auto joint_i = static_cast<size_t>(joint * static_cast<float>(size));
  // |joint_v| is the gamma output value where sRGB and linear curves meet.
  const float joint_v = powf(joint, gamma);
  // |slope| is the slope of the linear part.
  const float slope = (1 - joint_v) / (1 - joint);
  for (int i = 0; i < size; i++) {
    float v = static_cast<float>(i) / static_cast<float>(size - 1);

    if (i < joint_i)
      v = powf(v, gamma);
    else
      v = joint_v + slope * (v - joint);

    v *= 65535.0f;
    table[i] = static_cast<uint16_t>(v);
  }
}

static const char *gamma_name(const int gamma_table_id) {
  const char *table_names[] = {
    "TABLE_LINEAR", "TABLE_NEGATIVE", "TABLE_POW",
    "TABLE_STEP", "TABLE_PIECEWISE_HDR"
  };
  constexpr size_t max_gamma_table_id =
      sizeof(table_names) / sizeof(table_names[0]);
  if (gamma_table_id >= max_gamma_table_id)
    return nullptr;
  return table_names[gamma_table_id];
}

static void fsleep(const double secs) {
  usleep(static_cast<__useconds_t>(1000000.0f * secs));
}

static drmModeModeInfoPtr find_best_mode(const int mode_count, drmModeModeInfoPtr modes) {
  assert(mode_count >= 0);
  if (mode_count == 0)
    return nullptr;

  assert(modes);

  for (int m = 0; m < mode_count; m++)
    if (modes[m].type & DRM_MODE_TYPE_PREFERRED)
      return &modes[m];

  return &modes[0];
}

static bool draw_pattern(bs_mapper *mapper, gbm_bo *bo, const int gbm_format) {
  uint32_t stride;
  const uint32_t height = gbm_bo_get_height(bo);
  const uint32_t stripw = gbm_bo_get_width(bo) / 256;
  const uint32_t striph = height / 4;

  if (gbm_format != GBM_FORMAT_XRGB8888 && gbm_format != GBM_FORMAT_XRGB2101010)
    return false;

  void *map_data;
  auto *bo_ptr = static_cast<uint8_t *>(bs_mapper_map(mapper, bo, 0, &map_data, &stride));
  if (bo_ptr == MAP_FAILED) {
    bs_debug_error("failed to mmap buffer while drawing pattern");
    return false;
  }
  const uint32_t bo_size = stride * height;

  bool success = true;

  memset(bo_ptr, 0, bo_size);
  for (uint32_t s = 0; s < 4; s++) {
    uint8_t r = 0, g = 0, b = 0;
    switch (s) {
      case 0:
        r = g = b = 1;
        break;
      case 1:
        r = 1;
        break;
      case 2:
        g = 1;
        break;
      default:
      case 3:
        b = 1;
        break;
#if 0
        assert("invalid strip" && false);
        success = false;
        goto out;
#endif
    }
    for (uint32_t y = s * striph; y < (s + 1) * striph; y++) {
      uint8_t *row_ptr = &bo_ptr[y * stride];
      for (uint32_t i = 0; i < 256; i++) {
        for (uint32_t x = i * stripw; x < (i + 1) * stripw; x++) {
          if (gbm_format == GBM_FORMAT_XRGB8888) {
            row_ptr[x * 4 + 0] = b * i;
            row_ptr[x * 4 + 1] = g * i;
            row_ptr[x * 4 + 2] = r * i;
            row_ptr[x * 4 + 3] = 0;
          } else {
            *reinterpret_cast<uint32_t *>(&row_ptr[x * 4]) =
                (((r * i) << 2) | ((r * i) >> 6)) << 20 |
                (((g * i) << 2) | ((g * i) >> 6)) << 10 |
                (((b * i) << 2) | ((b * i) >> 6));
          }
        }
      }
    }
  }

  bs_mapper_unmap(mapper, bo, map_data);
  return success;
}

// |parameter| is passed to the appropriate |gamma_table| generation function.
static int set_gamma(const int fd,
                     const uint32_t crtc_id,
                     const int gamma_size,
                     const int gamma_table,
                     const float parameter) {
  auto *r = static_cast<uint16_t *>(calloc(gamma_size, sizeof(uint16_t)));
  auto *g = static_cast<uint16_t *>(calloc(gamma_size, sizeof(uint16_t)));
  auto *b = static_cast<uint16_t *>(calloc(gamma_size, sizeof(uint16_t)));

  printf("Setting gamma table index %d (%s)\n", gamma_table,
         gamma_name(gamma_table));
  switch (gamma_table) {
    case TABLE_LINEAR:
      gamma_linear(r, gamma_size);
      gamma_linear(g, gamma_size);
      gamma_linear(b, gamma_size);
      break;
    case TABLE_NEGATIVE:
      gamma_inv(r, gamma_size);
      gamma_inv(g, gamma_size);
      gamma_inv(b, gamma_size);
      break;
    case TABLE_POW:
      gamma_pow(r, gamma_size, parameter);
      gamma_pow(g, gamma_size, parameter);
      gamma_pow(b, gamma_size, parameter);
      break;
    case TABLE_STEP:
      gamma_step(r, gamma_size);
      gamma_step(g, gamma_size);
      gamma_step(b, gamma_size);
      break;
    case TABLE_PIECEWISE_HDR:
      gamma_piecewise_linear(r, gamma_size, parameter);
      gamma_piecewise_linear(g, gamma_size, parameter);
      gamma_piecewise_linear(b, gamma_size, parameter);
      break;
    default:
      break;
  }

  const int res = drmModeCrtcSetGamma(fd, crtc_id, gamma_size, r, g, b);
  if (res)
    bs_debug_error("drmModeCrtcSetGamma(%d) failed: %s", crtc_id,
                 strerror(errno));
  free(r);
  free(g);
  free(b);
  return res;
}

void help() {
  printf(
    "\
gamma test\n\
command line options:\
\n\
--help - this\n\
--linear - set linear gamma table\n\
--negative - set negative linear gamma table\n\
--step - set step gamma table\n\
--gamma=f - set pow(gamma) gamma table with gamma=f\n\
--time=f - set test time\n\
--crtcs=n - set mask of crtcs to test\n\
--persist - do not reset gamma table at the end of the test\n\
--internal - display tests on internal display\n\
--external - display tests on external display\n\
--hdr=x - sets piecewise HDR gamma table with SDR-HDR joint at x (usually 0.5)\n\
");
}

int main(const int argc, char **argv) {
  int internal = 1;
  int persist = 0;
  float time = 5.0f;
  float gamma = 2.2f;
  float srgb_joint = 0.0f;
  int table = TABLE_LINEAR;
  uint32_t crtcs = 0xFFFF;

  while (true) {
    const int c = getopt_long(argc, argv, "", command_options, nullptr);

    if (c == -1)
      break;

    switch (c) {
      case FLAG_HELP:
        help();
        return 0;

      case FLAG_INTERNAL:
        internal = 1;
        break;

      case FLAG_EXTERNAL:
        internal = 0;
        break;

      case FLAG_GAMMA:
        gamma = strtof(optarg, nullptr);
        table = TABLE_POW;
        break;

      case FLAG_LINEAR:
        table = TABLE_LINEAR;
        break;

      case FLAG_NEGATIVE:
        table = TABLE_NEGATIVE;
        break;

      case FLAG_STEP:
        table = TABLE_STEP;
        break;

      case FLAG_HDR:
        srgb_joint = strtof(optarg, nullptr);
        table = TABLE_PIECEWISE_HDR;
        break;

      case FLAG_TIME:
        time = strtof(optarg, nullptr);
        break;

      case FLAG_CRTCS:
        crtcs = strtoul(optarg, nullptr, 0);
        break;

      case FLAG_PERSIST:
        persist = 1;
        break;

      default:
        break;
    }
  }

  drmModeConnector *connector = nullptr;
  bs_drm_pipe pipe{};
  bs_drm_pipe_plumber *plumber = bs_drm_pipe_plumber_new();
  bs_drm_pipe_plumber_connector_ptr(plumber, &connector);
  bs_drm_pipe_plumber_crtc_mask(plumber, crtcs);
  if (!internal)
    bs_drm_pipe_plumber_connector_ranks(plumber,
                                        bs_drm_connectors_external_rank);
  if (!bs_drm_pipe_plumber_make(plumber, &pipe)) {
    bs_debug_error("failed to make pipe");
    return 1;
  }

  const int fd = pipe.fd;
  bs_drm_pipe_plumber_fd(plumber, fd);
  drmModeRes *resources = drmModeGetResources(fd);
  if (!resources) {
    bs_debug_error("failed to get drm resources");
    return 1;
  }

  gbm_device *gbm = gbm_create_device(fd);
  if (!gbm) {
    bs_debug_error("failed to create gbm");
    return 1;
  }

  bs_mapper *mapper = bs_mapper_gem_new();
  if (mapper == nullptr) {
    bs_debug_error("failed to create mapper object");
    return 1;
  }

  uint32_t num_success = 0;
  for (int c = 0; c < resources->count_crtcs && (crtcs >> c); c++) {
    const uint32_t crtc_mask = 1u << c;

    if (!(crtcs & crtc_mask))
      continue;

    if (c > 0)
      printf("\n");

    if (connector != nullptr) {
      drmModeFreeConnector(connector);
      connector = nullptr;
    }

    bs_drm_pipe_plumber_crtc_mask(plumber, crtc_mask);
    if (!bs_drm_pipe_plumber_make(plumber, &pipe)) {
      printf("unable to make pipe with crtc mask: %x\n", crtc_mask);
      continue;
    }

    drmModeCrtc *crtc = drmModeGetCrtc(fd, pipe.crtc_id);
    if (!crtc) {
      bs_debug_error("drmModeGetCrtc(%d) failed: %s\n", pipe.crtc_id,
                     strerror(errno));
      return 1;
    }
    const int gamma_size = crtc->gamma_size;
    drmModeFreeCrtc(crtc);

    if (!gamma_size) {
      bs_debug_error("CRTC %d has no gamma table", crtc->crtc_id);
      continue;
    }

    printf("CRTC:%d gamma table size:%d\n", pipe.crtc_id, gamma_size);

    printf("Using CRTC:%u ENCODER:%u CONNECTOR:%u\n", pipe.crtc_id,
           pipe.encoder_id, pipe.connector_id);

    const auto mode = find_best_mode(connector->count_modes, connector->modes);
    if (!mode) {
      bs_debug_error("Could not find mode for CRTC %d", pipe.crtc_id);
      continue;
    }

    printf("Using mode %s\n", mode->name);

    const bool is_hdr = table == TABLE_PIECEWISE_HDR;
    const int gbm_format =
        is_hdr ? GBM_FORMAT_XRGB2101010 : GBM_FORMAT_XRGB8888;

    printf("Creating buffer %ux%u (%s)\n", mode->hdisplay, mode->vdisplay,
           (is_hdr ? "XR30" : "XR24"));
    gbm_bo *bo = gbm_bo_create(gbm, mode->hdisplay, mode->vdisplay, gbm_format,
                               GBM_BO_USE_SCANOUT); // | GBM_BO_USE_SW_WRITE_RARELY);
    if (!bo) {
      bs_debug_error("failed to create buffer object");
      return 1;
    }

    const uint32_t fb_id = bs_drm_fb_create_gbm(bo);
    if (!fb_id) {
      bs_debug_error("failed to create frame buffer for buffer object");
      return 1;
    }

    if (!draw_pattern(mapper, bo, gbm_format)) {
      bs_debug_error("failed to draw pattern on buffer object");
      return 1;
    }

    int ret = drmModeSetCrtc(fd, pipe.crtc_id, fb_id, 0, 0, &pipe.connector_id, 1, mode);
    if (ret < 0) {
      bs_debug_error("Could not set mode on CRTC %d %s", pipe.crtc_id,
                     strerror(errno));
      return 1;
    }

    ret = set_gamma(fd, pipe.crtc_id, gamma_size, table,
                    is_hdr ? srgb_joint : gamma);
    if (ret)
      return ret;

    fsleep(time);

    if (!persist) {
      ret = set_gamma(fd, pipe.crtc_id, gamma_size, TABLE_LINEAR, 0.0f);
      if (ret)
        return ret;
    }

    ret = drmModeSetCrtc(fd, pipe.crtc_id, 0, 0, 0, nullptr, 0, nullptr);
    if (ret < 0) {
      bs_debug_error("Could disable CRTC %d %s\n", pipe.crtc_id,
                     strerror(errno));
      return 1;
    }

    drmModeRmFB(fd, fb_id);
    gbm_bo_destroy(bo);
    num_success++;
  }
  bs_mapper_destroy(mapper);

  if (connector != nullptr) {
    drmModeFreeConnector(connector);
    connector = nullptr;
  }

  drmModeFreeResources(resources);
  bs_drm_pipe_plumber_destroy(&plumber);

  if (!num_success) {
    bs_debug_error("unable to set gamma table on any CRTC");
    return 1;
  }

  return 0;
}
