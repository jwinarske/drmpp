/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright 2017 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <getopt.h>

extern "C" {
#include "bs_drm.h"
}

#define BUFFERS 2
#define NUM_FRAMES 0x40

struct framebuffer {
  gbm_bo *bo;
  uint32_t id;
};

struct context {
  int display_fd;
  uint32_t crtc_id;

  framebuffer fbs[BUFFERS];
  bs_mapper *mapper;

  int vgem_device_fd;
};

struct disabled_features {
  char psr_prev_val[1];
};

constexpr char psr_path[] = "/sys/module/i915/parameters/enable_psr";

static void disable_psr(disabled_features *dis_feat) {
  const int psr_fd = open(psr_path, O_RDWR);

  if (psr_fd < 0)
    return;

  if (read(psr_fd, dis_feat->psr_prev_val, 1) == -1) {
    bs_debug_error("failed to read psr value");
  } else {
    printf("previous enable_psr value: %c\n", dis_feat->psr_prev_val[0]);
  }
  if (strncmp(dis_feat->psr_prev_val, "1", 1) == 0) {
    if (write(psr_fd, "0", 1) == -1) {
      bs_debug_error("failed to disable psr");
    } else {
      printf("disabled psr\n");
    }
  }

  close(psr_fd);
}

static void restore_psr(const disabled_features *dis_feat) {
  if (strncmp(dis_feat->psr_prev_val, "1", 1) == 0) {
    const int psr_fd = open(psr_path, O_WRONLY);

    if (psr_fd < 0)
      return;

    if (write(psr_fd, "1", 1) == -1) {
      bs_debug_error("failed to re-enable psr");
    } else {
      printf("re-enabled psr\n");
    }

    close(psr_fd);
  }
}

static void do_fixes(disabled_features *dis_feat) {
  disable_psr(dis_feat);
}

static void restore_features(const disabled_features *dis_feat) {
  restore_psr(dis_feat);
}

#define STEP_SKIP 0
#define STEP_MMAP 1
#define STEP_FAULT 2
#define STEP_FLIP 3
#define STEP_DRAW 4

static void show_sequence(const int *sequence) {
  printf("starting sequence: ");
  for (int sequence_subindex = 0; sequence_subindex < 4; sequence_subindex++) {
    switch (sequence[sequence_subindex]) {
      case STEP_SKIP:
        break;
      case STEP_MMAP:
        printf("mmap ");
        break;
      case STEP_FAULT:
        printf("fault ");
        break;
      case STEP_FLIP:
        printf("flip ");
        break;
      case STEP_DRAW:
        printf("draw ");
        break;
      default:
        bs_debug_error("<unknown step %d> (aborting!)",
                       sequence[sequence_subindex]);
        abort();
    }
  }
  printf("\n");
}

static void draw(const context *ctx) {
  // Run the drawing routine with the key driver events in different
  // sequences.
  constexpr int sequences[4][4] = {
    {STEP_MMAP, STEP_FAULT, STEP_FLIP, STEP_DRAW},
    {STEP_MMAP, STEP_FLIP, STEP_DRAW, STEP_SKIP},
    {STEP_MMAP, STEP_DRAW, STEP_FLIP, STEP_SKIP},
    {STEP_FLIP, STEP_MMAP, STEP_DRAW, STEP_SKIP},
  };

  int sequence_index = 0;
  int sequence_subindex = 0;

  int fb_idx = 1;

  for (sequence_index = 0; sequence_index < 4; sequence_index++) {
    show_sequence(sequences[sequence_index]);
    for (int frame_index = 0; frame_index < NUM_FRAMES; frame_index += 2) {
      const auto *fb = const_cast<struct framebuffer *>(&ctx->fbs[fb_idx]);
      uint32_t bo_stride;
      const uint32_t width = gbm_bo_get_width(fb->bo);
      const uint32_t height = gbm_bo_get_height(fb->bo);
      const uint32_t stride = gbm_bo_get_stride(fb->bo);
      const size_t bo_size = static_cast<size_t>(stride) * height;
      auto *bo_ptr = static_cast<uint32_t *>(nullptr);
      volatile uint32_t *ptr = nullptr;
      void *map_data;

      for (sequence_subindex = 0; sequence_subindex < 4; sequence_subindex++) {
        switch (sequences[sequence_index][sequence_subindex]) {
          case STEP_MMAP:
            bo_ptr =
                static_cast<uint32_t *>(bs_mapper_map(ctx->mapper, fb->bo, 0, &map_data, &bo_stride));
            if (bo_ptr == MAP_FAILED)
              bs_debug_error("failed to mmap gbm bo");

            assert(bo_ptr != MAP_FAILED && bo_stride == stride);
            ptr = bo_ptr;
            break;

          case STEP_FAULT:
            *ptr = 1234567;
            break;

          case STEP_FLIP:
            drmModePageFlip(ctx->display_fd, ctx->crtc_id, ctx->fbs[fb_idx].id,
                            0, nullptr);
            break;

          case STEP_DRAW:
            if (bo_ptr == nullptr)
              break;
            for (ptr = bo_ptr; ptr < bo_ptr + (bo_size / sizeof(*bo_ptr));
                 ptr++) {
              int y = static_cast<int>(static_cast<std::ptrdiff_t>(ptr - bo_ptr) / static_cast<std::ptrdiff_t>(stride));
              int x = static_cast<int>((static_cast<std::ptrdiff_t>(ptr - bo_ptr) - static_cast<std::ptrdiff_t>(stride)
                                        * y) / sizeof(*ptr));
              x -= static_cast<int>(static_cast<std::ptrdiff_t>(frame_index) * (
                                      static_cast<std::ptrdiff_t>(width) / NUM_FRAMES));
              y -= static_cast<int>(static_cast<std::ptrdiff_t>(frame_index) * (
                                      static_cast<std::ptrdiff_t>(height) / NUM_FRAMES));
              *ptr = 0xff000000;
              if (x * x + y * y < frame_index * frame_index) {
                *ptr |= (frame_index % 0x100) << 8;
              } else {
                *ptr |= 0xff | (sequence_index * 64 << 16);
              }
            }
            break;

          case STEP_SKIP:
          default:
            break;
        }
      }

      bs_mapper_unmap(ctx->mapper, fb->bo, map_data);

      usleep(static_cast<__useconds_t>(1e6 / 120)); /* 120 Hz */

      fb_idx = fb_idx ^ 1;
    }
  }
}

static const option longopts[] = {
  {"help", no_argument, nullptr, 'h'},
  {"dma-buf", no_argument, nullptr, 'b'},
  {"gem", no_argument, nullptr, 'g'},
  {"dumb", no_argument, nullptr, 'd'},
  {"vgem", no_argument, nullptr, 'v'},
  {"scanout", no_argument, nullptr, 's'},
  {nullptr, 0, nullptr, 0},
};

static void print_help(const char *argv0) {
  printf("Usage: %s [OPTIONS]\n", argv0);
  printf(" -h, --help     Print help.\n");
  printf(" -b, --dma-buf  Use dma-buf mmap (by default).\n");
  printf(" -g, --gem      Use GEM map.\n");
  printf(" -d, --dumb     Use dump map.\n");
  printf(" -v, --vgem     Use vgem dump map.\n");
  printf(" -s, --scanout  Use buffer optimized for scanout.\n");
}

int main(const int argc, char **argv) {
  context ctx{};
  disabled_features dis_feat{};

  do_fixes(&dis_feat);

  ctx.display_fd = bs_drm_open_main_display();
  if (ctx.display_fd < 0) {
    bs_debug_error("failed to open card for display");
    return 1;
  }

  gbm_device *gbm = gbm_create_device(ctx.display_fd);
  if (!gbm) {
    bs_debug_error("failed to create gbm device");
    return 1;
  }

  int c;
  uint32_t flags = GBM_BO_USE_SCANOUT;
  while ((c = getopt_long(argc, argv, "bgdvsh", longopts, nullptr)) != -1) {
    switch (c) {
      case 'b':
        ctx.mapper = bs_mapper_dma_buf_new();
        flags |= GBM_BO_USE_LINEAR;
        printf("started dma-buf mmap.\n");
        break;
      case 'g':
        ctx.mapper = bs_mapper_gem_new();
#if defined(GBM_BO_USE_SW_READ_OFTEN) && defined(GBM_BO_USE_SW_WRITE_OFTEN)
        flags |= GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN;
#endif
        printf("started GEM map.\n");
        break;
      case 'd':
        ctx.mapper = bs_mapper_dumb_new(gbm_device_get_fd(gbm));
        flags |= GBM_BO_USE_LINEAR;
        printf("started dumb map.\n");
        break;
      case 'v':
        ctx.vgem_device_fd = bs_drm_open_vgem();
        ctx.mapper = bs_mapper_dumb_new(ctx.vgem_device_fd);
        printf("started vgem map.\n");
        break;
      case 's':
        flags = GBM_BO_USE_SCANOUT;
        break;
      case 'h':
      default:
        print_help(argv[0]);
        return 1;
    }
  }

  // Use dma-buf mmap by default, in case any arguments aren't selected.
  if (!ctx.mapper) {
    ctx.mapper = bs_mapper_dma_buf_new();
    flags |= GBM_BO_USE_LINEAR;
    printf("started dma-buf mmap.\n");
  }

  if (ctx.mapper == nullptr) {
    bs_debug_error("failed to create mapper object");
    return 1;
  }

  bs_drm_pipe pipe{};
  if (!bs_drm_pipe_make(ctx.display_fd, &pipe)) {
    bs_debug_error("failed to make pipe");
    return 1;
  }

  const auto *connector = drmModeGetConnector(ctx.display_fd, pipe.connector_id);
  drmModeModeInfo *mode = &connector->modes[0];
  ctx.crtc_id = pipe.crtc_id;

  printf("display size: %ux%u\n", mode->hdisplay, mode->vdisplay);

  for (auto &fb_index: ctx.fbs) {
    framebuffer *fb = &fb_index;
    fb->bo = gbm_bo_create(gbm, mode->hdisplay, mode->vdisplay,
                           GBM_FORMAT_XRGB8888, flags);

    if (!fb->bo) {
      bs_debug_error("failed to create buffer object");
      return 1;
    }

    fb->id = bs_drm_fb_create_gbm(fb->bo);
    if (fb->id == 0) {
      bs_debug_error("failed to create fb");
      return 1;
    }
  }

  if (drmModeSetCrtc(ctx.display_fd, pipe.crtc_id, ctx.fbs[0].id, 0, 0,
                     &pipe.connector_id, 1, mode)) {
    bs_debug_error("failed to set CRTC");
    return 1;
  }

  draw(&ctx);

  restore_features(&dis_feat);

  return 0;
}
