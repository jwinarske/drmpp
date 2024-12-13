/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright 2012 The ChromiumOS Authors
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */


#include <cassert>
#include <cerrno>
#include <fcntl.h>
#include <linux/dma-buf.h>

#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#include <gbm.h>

#include "logging/logging.h"

#define CHECK(cond)                                                         \
  do {                                                                      \
    if (!(cond)) {                                                          \
      printf("CHECK failed in %s() %s:%d\n", __func__, __FILE__, __LINE__); \
      return 0;                                                             \
    }                                                                       \
  } while (0)

#define HANDLE_EINTR(x)                                      \
  ({                                                         \
    int eintr_wrapper_counter = 0;                           \
    int eintr_wrapper_result;                                \
    do {                                                     \
      eintr_wrapper_result = (x);                            \
    } while (eintr_wrapper_result == -1 && errno == EINTR && \
             eintr_wrapper_counter++ < 100);                 \
    eintr_wrapper_result;                                    \
  })

#define ARRAY_SIZE(A) (sizeof(A) / sizeof(*(A)))

#define ENODRM (-1)

static int fd;
static gbm_device *gbm_device;

static constexpr uint32_t format_list[] = {
  GBM_FORMAT_R8, GBM_FORMAT_RGB565, GBM_FORMAT_BGR888,
  GBM_FORMAT_XRGB8888, GBM_FORMAT_XBGR8888, GBM_FORMAT_ARGB8888,
  GBM_FORMAT_ABGR8888, GBM_FORMAT_XRGB2101010, GBM_FORMAT_XBGR2101010,
  GBM_FORMAT_ARGB2101010, GBM_FORMAT_ABGR2101010, GBM_FORMAT_ABGR16161616F,
  GBM_FORMAT_NV12, GBM_FORMAT_YVU420,
};

struct format_info {
  uint32_t pixel_format;
  uint32_t bits_per_pixel;
  uint32_t data_mask;
};

/* Bits per pixel for each. */
static const format_info mappable_format_list[] = {
  {GBM_FORMAT_R8, 8, 0xFF},
  {GBM_FORMAT_RGB565, 16, 0xFFFF},
  {GBM_FORMAT_BGR888, 24, 0xFFFFFF},
  {GBM_FORMAT_XRGB8888, 32, 0x00FFFFFF},
  {GBM_FORMAT_XBGR8888, 32, 0x00FFFFFF},
  {GBM_FORMAT_ARGB8888, 32, 0xFFFFFFFF},
  {GBM_FORMAT_ABGR8888, 32, 0xFFFFFFFF},
  {GBM_FORMAT_XRGB2101010, 32, 0x3FFFFFFF},
  {GBM_FORMAT_XBGR2101010, 32, 0x3FFFFFFF},
  {GBM_FORMAT_ARGB2101010, 32, 0xFFFFFFFF},
  {GBM_FORMAT_ABGR2101010, 32, 0xFFFFFFFF},
};

static constexpr uint32_t usage_list[] = {
  GBM_BO_USE_SCANOUT,
  GBM_BO_USE_CURSOR_64X64,
  GBM_BO_USE_RENDERING,
  GBM_BO_USE_LINEAR,
#if defined(GBM_BO_USE_SW_READ_OFTEN) && defined(GBM_BO_USE_SW_WRITE_OFTEN)
  GBM_BO_USE_SW_READ_OFTEN, GBM_BO_USE_SW_WRITE_OFTEN,
#endif
};

static constexpr gbm_bo_flags mappable_usage_list[] = {
  GBM_BO_USE_SCANOUT
#if defined(GBM_BO_USE_SW_READ_OFTEN) && defined(GBM_BO_USE_SW_WRITE_OFTEN)
  | GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN
#endif
  ,
  GBM_BO_USE_RENDERING
#if defined(GBM_BO_USE_SW_READ_OFTEN) && defined(GBM_BO_USE_SW_WRITE_OFTEN)
  | GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN
#endif
  ,
#if defined(GBM_BO_USE_TEXTURING) && defined(GBM_BO_USE_SW_READ_OFTEN) && defined(GBM_BO_USE_SW_WRITE_OFTEN)
  GBM_BO_USE_TEXTURING | GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN,
#endif
};

static int check_bo(gbm_bo *bo) {
  int i;

  CHECK(bo);
  CHECK(gbm_bo_get_width(bo) > 0);
  CHECK(gbm_bo_get_height(bo) > 0);
  CHECK(gbm_bo_get_stride(bo) >= gbm_bo_get_width(bo));

  const uint32_t format = gbm_bo_get_format(bo);
  for (i = 0; i < std::size(format_list); i++)
    if (format_list[i] == format)
      break;
  CHECK(i < ARRAY_SIZE(format_list));

  const size_t num_planes = gbm_bo_get_plane_count(bo);
  if (format == GBM_FORMAT_NV12)
    CHECK(num_planes == 2);
  else if (format == GBM_FORMAT_YVU420)
    CHECK(num_planes == 3);
  else
    CHECK(num_planes == 1);

  CHECK(gbm_bo_get_handle_for_plane(bo, 0).u32 == gbm_bo_get_handle(bo).u32);

  CHECK(gbm_bo_get_offset(bo, 0) == 0);
  CHECK(gbm_bo_get_stride_for_plane(bo, 0) == gbm_bo_get_stride(bo));

  for (size_t plane = 0; plane < num_planes; plane++) {
    CHECK(gbm_bo_get_handle_for_plane(bo, plane).u32);

    const int fd = gbm_bo_get_fd_for_plane(bo, static_cast<int>(plane));
    CHECK(fd > 0);
    close(fd);

    gbm_bo_get_offset(bo, static_cast<int>(plane));
    CHECK(gbm_bo_get_stride_for_plane(bo, plane));
  }

  return 1;
}

static drmModeConnector *find_first_connected_connector(const int fd,
                                                        const drmModeRes *resources) {
  for (int i = 0; i < resources->count_connectors; i++) {
    if (drmModeConnector *connector = drmModeGetConnector(fd, resources->connectors[i])) {
      if ((connector->count_modes > 0) &&
          (connector->connection == DRM_MODE_CONNECTED))
        return connector;

      drmModeFreeConnector(connector);
    }
  }
  return nullptr;
}

static int drm_open() {
  int fd;
  unsigned i;

  /* Find the first drm device with a connected display. */
  for (i = 0; i < DRM_MAX_MINOR; i++) {
    char *dev_name;
    drmModeRes *res = nullptr;

    if (int ret = asprintf(&dev_name, DRM_DEV_NAME, DRM_DIR_NAME, i); ret < 0)
      continue;

    fd = open(dev_name, O_RDWR, 0);
    free(dev_name);
    if (fd < 0)
      continue;

    res = drmModeGetResources(fd);
    if (!res) {
      drmClose(fd);
      continue;
    }

    if (res->count_crtcs > 0 && res->count_connectors > 0) {
      if (find_first_connected_connector(fd, res)) {
        drmModeFreeResources(res);
        return fd;
      }
    }

    drmClose(fd);
    drmModeFreeResources(res);
  }

  /*
   * If no drm device has a connected display, fall back to the first
   * drm device.
   */
  for (i = 0; i < DRM_MAX_MINOR; i++) {
    char *dev_name;

    if (int ret = asprintf(&dev_name, DRM_DEV_NAME, DRM_DIR_NAME, i); ret < 0)
      continue;

    fd = open(dev_name, O_RDWR, 0);
    free(dev_name);
    if (fd < 0)
      continue;

    return fd;
  }

  return ENODRM;
}

/*
 * Tests initialization.
 */
static int test_init() {
  fd = drm_open();

  CHECK(fd >= 0);

  gbm_device = gbm_create_device(fd);

  CHECK(gbm_device_get_fd(gbm_device) == fd);

  const char *backend_name = gbm_device_get_backend_name(gbm_device);

  CHECK(backend_name);

  return 1;
}

/*
 * Tests reinitialization.
 */
static int test_reinit() {
  gbm_device_destroy(gbm_device);
  close(fd);

  fd = drm_open();
  CHECK(fd >= 0);

  gbm_device = gbm_create_device(fd);

  CHECK(gbm_device_get_fd(gbm_device) == fd);

  gbm_bo *bo = gbm_bo_create(gbm_device, 1024, 1024, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
  CHECK(check_bo(bo));
  gbm_bo_destroy(bo);

  return 1;
}

/*
 * Tests repeated alloc/free.
 */
static int test_alloc_free() {
  for (int i = 0; i < 1000; i++) {
    gbm_bo *bo = gbm_bo_create(gbm_device, 1024, 1024, GBM_FORMAT_XRGB8888,
                               GBM_BO_USE_RENDERING);
    CHECK(check_bo(bo));
    gbm_bo_destroy(bo);
  }
  return 1;
}

/*
 * Tests that we can allocate different buffer dimensions.
 */
static int test_alloc_free_sizes() {
  for (int i = 1; i < 1920; i++) {
    gbm_bo *bo = gbm_bo_create(gbm_device, i, i, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    CHECK(check_bo(bo));
    gbm_bo_destroy(bo);
  }

  for (int i = 1; i < 1920; i++) {
    gbm_bo *bo = gbm_bo_create(gbm_device, i, 1, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    CHECK(check_bo(bo));
    gbm_bo_destroy(bo);
  }

  for (int i = 1; i < 1920; i++) {
    gbm_bo *bo = gbm_bo_create(gbm_device, 1, i, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
    CHECK(check_bo(bo));
    gbm_bo_destroy(bo);
  }

  return 1;
}

/*
 * Tests that we can allocate different buffer formats.
 */
static int test_alloc_free_formats() {
  for (const unsigned int format: format_list) {
    if (gbm_device_is_format_supported(gbm_device, format, GBM_BO_USE_RENDERING)) {
      gbm_bo *bo = gbm_bo_create(gbm_device, 1024, 1024, format, GBM_BO_USE_RENDERING);
      CHECK(check_bo(bo));
      gbm_bo_destroy(bo);
    }
  }

  return 1;
}

/*
 * Tests that we find at least one working format for each usage.
 */
static int test_alloc_free_usage() {
  for (const unsigned int usage: usage_list) {
    int found = 0;
    for (const unsigned int format: format_list) {
      if (gbm_device_is_format_supported(gbm_device, format, usage)) {
        gbm_bo *bo;
        if (usage == GBM_BO_USE_CURSOR_64X64)
          bo = gbm_bo_create(gbm_device, 64, 64, format, usage);
        else
          bo = gbm_bo_create(gbm_device, 1024, 1024, format, usage);
        CHECK(check_bo(bo));
        found = 1;
        gbm_bo_destroy(bo);
      }
    }
    CHECK(found);
  }

  return 1;
}

/*
 * Tests user data.
 */
static int been_there1;
static int been_there2;

void destroy_data1(gbm_bo *bo, void *data) {
  been_there1 = 1;
}

void destroy_data2(gbm_bo *bo, void *data) {
  been_there2 = 1;
}

static int test_user_data() {
  //been_there1 = 0;
  //been_there2 = 0;

  gbm_bo *bo1 = gbm_bo_create(gbm_device, 1024, 1024, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
  gbm_bo *bo2 = gbm_bo_create(gbm_device, 1024, 1024, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
  const auto data1 = static_cast<char *>(malloc(1));
  const auto data2 = static_cast<char *>(malloc(1));
  CHECK(data1);
  CHECK(data2);

  gbm_bo_set_user_data(bo1, data1, destroy_data1);
  gbm_bo_set_user_data(bo2, data2, destroy_data2);

  CHECK(static_cast<char *>(gbm_bo_get_user_data(bo1)) == data1);
  CHECK(static_cast<char *>(gbm_bo_get_user_data(bo2)) == data2);

  gbm_bo_destroy(bo1);
  //CHECK(been_there1 == 1);

  gbm_bo_set_user_data(bo2, nullptr, nullptr);
  gbm_bo_destroy(bo2);
  //CHECK(been_there2 == 0);

  free(data1);
  free(data2);

  return 1;
}

/*
 * Tests destruction.
 */
static int test_destroy() {
  gbm_device_destroy(gbm_device);
  close(fd);

  return 1;
}

/*
 * Tests prime export.
 */
static int test_export() {
  gbm_bo *bo = gbm_bo_create(gbm_device, 1024, 1024, GBM_FORMAT_XRGB8888, GBM_BO_USE_RENDERING);
  CHECK(check_bo(bo));

  const int prime_fd = gbm_bo_get_fd(bo);
  CHECK(prime_fd > 0);
  close(prime_fd);

  gbm_bo_destroy(bo);

  return 1;
}

/*
 * Tests prime import using dma-buf API.
 */
static int test_import_dmabuf() {
  gbm_import_fd_data fd_data{};
  constexpr int width = 123;
  constexpr int height = 456;

  gbm_bo *bo1 = gbm_bo_create(gbm_device, width, height, GBM_FORMAT_XRGB8888,
                              GBM_BO_USE_RENDERING);
  CHECK(check_bo(bo1));

  const int prime_fd = gbm_bo_get_fd(bo1);
  CHECK(prime_fd >= 0);

  fd_data.fd = prime_fd;
  fd_data.width = width;
  fd_data.height = height;
  fd_data.stride = gbm_bo_get_stride(bo1);
  fd_data.format = GBM_FORMAT_XRGB8888;

  gbm_bo_destroy(bo1);

  gbm_bo *bo2 = gbm_bo_import(gbm_device, GBM_BO_IMPORT_FD, &fd_data, GBM_BO_USE_RENDERING);
  CHECK(check_bo(bo2));
  CHECK(fd_data.width == gbm_bo_get_width(bo2));
  CHECK(fd_data.height == gbm_bo_get_height(bo2));
  CHECK(fd_data.stride == gbm_bo_get_stride(bo2));

  gbm_bo_destroy(bo2);
  close(prime_fd);

  return 1;
}

/*
 * Tests GBM_BO_IMPORT_FD_MODIFIER entry point.
 */
static int test_import_modifier() {
  gbm_import_fd_modifier_data fd_data{};

  for (const unsigned int format: format_list) {
    if (gbm_device_is_format_supported(gbm_device, format, GBM_BO_USE_RENDERING)) {
      constexpr int height = 891;
      constexpr int width = 567;
      gbm_bo *bo1 = gbm_bo_create(gbm_device, width, height, format, GBM_BO_USE_RENDERING);
      CHECK(check_bo(bo1));

      const size_t num_planes = gbm_bo_get_plane_count(bo1);
      fd_data.num_fds = num_planes;

      for (auto p = 0; p < num_planes; p++) {
        fd_data.fds[p] = gbm_bo_get_fd_for_plane(bo1, p);
        CHECK(fd_data.fds[p] >= 0);

        fd_data.strides[p] = static_cast<int>(gbm_bo_get_stride_for_plane(bo1, p));
        fd_data.offsets[p] = static_cast<int>(gbm_bo_get_offset(bo1, p));
      }

      fd_data.modifier = gbm_bo_get_modifier(bo1);
      fd_data.width = width;
      fd_data.height = height;
      fd_data.format = format;

      gbm_bo_destroy(bo1);

      gbm_bo *bo2 = gbm_bo_import(gbm_device, GBM_BO_IMPORT_FD_MODIFIER, &fd_data,
                                  GBM_BO_USE_RENDERING);

      CHECK(check_bo(bo2));
      CHECK(fd_data.width == gbm_bo_get_width(bo2));
      CHECK(fd_data.height == gbm_bo_get_height(bo2));
      CHECK(fd_data.modifier == gbm_bo_get_modifier(bo2));

      for (auto p = 0; p < num_planes; p++) {
        CHECK(fd_data.strides[p] == gbm_bo_get_stride_for_plane(bo2, p));
        CHECK(fd_data.offsets[p] == gbm_bo_get_offset(bo2, p));
      }

      gbm_bo_destroy(bo2);

      for (auto p = 0; p < num_planes; p++) {
        close(fd_data.fds[p]);
      }
    }
  }

  return 1;
}

static int test_gem_map() {
  uint32_t stride = 0;
  constexpr int width = 666;
  constexpr int height = 777;

  gbm_bo *bo = gbm_bo_create(gbm_device, width, height, GBM_FORMAT_ARGB8888,
#if defined(GBM_BO_USE_SW_READ_OFTEN) && defined(GBM_BO_USE_SW_WRITE_OFTEN)
  GBM_BO_USE_SW_READ_OFTEN | GBM_BO_USE_SW_WRITE_OFTEN
#else
                             0
#endif
  );
  CHECK(check_bo(bo));

  void *map_data = nullptr;
  void *addr = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_READ_WRITE,
                          &stride, &map_data);

  CHECK(addr != MAP_FAILED);
  CHECK(map_data);
  CHECK(stride > 0);

  auto *pixel = static_cast<uint32_t *>(addr);
  constexpr uint32_t pixel_size = sizeof(*pixel);

  pixel[(height / 2) * (stride / pixel_size) + width / 2] = 0xABBAABBA;
  gbm_bo_unmap(bo, map_data);

  /* Re-map and verify written previously data. */
  stride = 0;
  addr = map_data = nullptr;

  addr = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_READ_WRITE,
                    &stride, &map_data);

  CHECK(addr != MAP_FAILED);
  CHECK(map_data);
  CHECK(stride > 0);

  pixel = static_cast<uint32_t *>(addr);
  CHECK(pixel[(height / 2) * (stride / pixel_size) + width / 2] == 0xABBAABBA);

  gbm_bo_unmap(bo, map_data);
  gbm_bo_destroy(bo);

  return 1;
}

static int test_dmabuf_map() {
  void *map_data;
  constexpr int width = 666;
  constexpr int height = 777;
  int x, y;
  dma_buf_sync sync_end{};
  dma_buf_sync sync_start{};
  uint32_t stride;

  gbm_bo *bo = gbm_bo_create(gbm_device, width, height, GBM_FORMAT_ARGB8888, GBM_BO_USE_LINEAR);
  CHECK(check_bo(bo));

  int prime_fd = gbm_bo_get_fd(bo);
  CHECK(prime_fd > 0);

  stride = gbm_bo_get_stride(bo);
  const auto length = static_cast<uint32_t>(lseek(prime_fd, 0, SEEK_END));;
  CHECK(stride > 0);
  CHECK(length > 0);

  void *addr = mmap(nullptr, length, (PROT_READ | PROT_WRITE), MAP_SHARED, prime_fd, 0);
  CHECK(addr != MAP_FAILED);

  auto *pixel = static_cast<uint32_t *>(addr);
  constexpr uint32_t pixel_size = sizeof(*pixel);
  const uint32_t stride_pixels = stride / pixel_size;

  sync_start.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_WRITE;
  int ret = HANDLE_EINTR(ioctl(prime_fd, DMA_BUF_IOCTL_SYNC, &sync_start));
  CHECK(ret == 0);

  for (y = 0; y < height; ++y)
    for (x = 0; x < width; ++x)
      pixel[y * stride_pixels + x] = ((y << 16) | x);

  sync_end.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_WRITE;
  ret = HANDLE_EINTR(ioctl(prime_fd, DMA_BUF_IOCTL_SYNC, &sync_end));
  CHECK(ret == 0);

  ret = munmap(addr, length);
  CHECK(ret == 0);

  ret = close(prime_fd);
  CHECK(ret == 0);

  prime_fd = gbm_bo_get_fd(bo);
  CHECK(prime_fd > 0);

  addr = mmap(nullptr, length, (PROT_READ | PROT_WRITE), MAP_SHARED, prime_fd, 0);
  CHECK(addr != MAP_FAILED);

  pixel = static_cast<uint32_t *>(addr);

  memset(&sync_start, 0, sizeof(sync_start));
  memset(&sync_end, 0, sizeof(sync_end));

  sync_start.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
  ret = HANDLE_EINTR(ioctl(prime_fd, DMA_BUF_IOCTL_SYNC, &sync_start));
  CHECK(ret == 0);

  for (y = 0; y < height; ++y)
    for (x = 0; x < width; ++x)
      CHECK(pixel[y * stride_pixels + x] == ((y << 16) | x));

  sync_end.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
  ret = HANDLE_EINTR(ioctl(prime_fd, DMA_BUF_IOCTL_SYNC, &sync_end));
  CHECK(ret == 0);

  ret = munmap(addr, length);
  CHECK(ret == 0);

  ret = close(prime_fd);
  CHECK(ret == 0);

  addr = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_READ, &stride,
                    &map_data);

  CHECK(addr != MAP_FAILED);
  CHECK(map_data);
  CHECK(stride > 0);

  pixel = static_cast<uint32_t *>(addr);

  for (y = 0; y < height; ++y)
    for (x = 0; x < width; ++x)
      CHECK(pixel[y * stride_pixels + x] == ((y << 16) | x));

  gbm_bo_unmap(bo, map_data);
  gbm_bo_destroy(bo);

  return 1;
}

static int test_gem_map_tiling(const gbm_bo_flags buffer_create_flag) {
  uint32_t stride = 0;
  uint32_t stride_pixels = 0;
  constexpr int width = 666;
  constexpr int height = 777;
  int x, y;

  gbm_bo *bo = gbm_bo_create(gbm_device, width, height, GBM_FORMAT_ARGB8888,
                             buffer_create_flag);
  CHECK(check_bo(bo));

  void *map_data = nullptr;
  void *addr = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_WRITE, &stride,
                          &map_data);

  CHECK(addr != MAP_FAILED);
  CHECK(map_data);
  CHECK(stride > 0);

  auto *pixel = static_cast<uint32_t *>(addr);
  uint32_t pixel_size = sizeof(*pixel);
  stride_pixels = stride / pixel_size;

  for (y = 0; y < height; ++y)
    for (x = 0; x < width; ++x)
      pixel[y * stride_pixels + x] = ((y << 16) | x);

  gbm_bo_unmap(bo, map_data);

  /* Re-map and verify written previously data. */
  stride = 0;
  addr = map_data = nullptr;

  addr = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_READ, &stride,
                    &map_data);

  CHECK(addr != MAP_FAILED);
  CHECK(map_data);
  CHECK(stride > 0);

  pixel = static_cast<uint32_t *>(addr);
  pixel_size = sizeof(*pixel);
  stride_pixels = stride / pixel_size;

  for (y = 0; y < height; ++y)
    for (x = 0; x < width; ++x)
      CHECK(pixel[y * stride_pixels + x] == ((y << 16) | x));

  gbm_bo_unmap(bo, map_data);
  gbm_bo_destroy(bo);

  return 1;
}

static int test_gem_map_format(const int format_index,
                               const gbm_bo_flags buffer_create_flag) {
  void *map_data;
  uint32_t x, y, b, idx;
  uint32_t stride = 0;
  constexpr int width = 333;
  constexpr int height = 444;
  const uint32_t pixel_format = mappable_format_list[format_index].pixel_format;

  void *addr = map_data = nullptr;
  if (!gbm_device_is_format_supported(gbm_device, pixel_format, buffer_create_flag))
    return 1;

  gbm_bo *bo = gbm_bo_create(gbm_device, width, height, pixel_format, buffer_create_flag);
  CHECK(check_bo(bo));

  addr = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_WRITE, &stride,
                    &map_data);

  CHECK(addr != MAP_FAILED);
  CHECK(map_data);
  CHECK(stride > 0);

  auto *pixel = static_cast<uint8_t *>(addr);
  const uint32_t bytes_per_pixel = mappable_format_list[format_index].bits_per_pixel / 8;
  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      idx = y * stride + x * bytes_per_pixel;
      for (b = 0; b < bytes_per_pixel; ++b)
        pixel[idx + b] = y ^ x ^ b;
    }
  }
  gbm_bo_unmap(bo, map_data);
  stride = 0;
  addr = map_data = nullptr;

  /* Re-map and verify written previously data. */
  addr = gbm_bo_map(bo, 0, 0, width, height, GBM_BO_TRANSFER_READ, &stride,
                    &map_data);

  CHECK(addr != MAP_FAILED);
  CHECK(map_data);
  CHECK(stride > 0);

  pixel = static_cast<uint8_t *>(addr);
  const uint32_t pixel_data_mask = mappable_format_list[format_index].data_mask;
  for (y = 0; y < height; ++y) {
    for (x = 0; x < width; ++x) {
      idx = y * stride + x * bytes_per_pixel;
      for (b = 0; b < bytes_per_pixel; ++b) {
        const uint8_t byte_mask = pixel_data_mask >> (8 * b);
        CHECK((pixel[idx + b] & byte_mask) ==
          (static_cast<uint8_t>(y ^ x ^ b)& byte_mask));
      }
    }
  }
  gbm_bo_unmap(bo, map_data);
  stride = 0;
  addr = map_data = nullptr;

  gbm_bo_destroy(bo);
  return 1;
}

int main(int argc, char *argv[]) {
  int result = test_init();
  if (result != 1) {
    printf("[  FAILED  ] graphics_Gbm test initialization failed\n");
    return EXIT_FAILURE;
  }

  result &= test_reinit();
  result &= test_alloc_free();
  result &= test_alloc_free_sizes();
  result &= test_alloc_free_formats();
  result &= test_alloc_free_usage();
  result &= test_user_data();
  result &= test_export();
  result &= test_import_dmabuf();
  result &= test_import_modifier();
  result &= test_gem_map();

  // TODO(crbug.com/752669)
  if (strcmp(gbm_device_get_backend_name(gbm_device), "tegra") != 0) {
    for (const auto i: mappable_usage_list) {
      result &= test_gem_map_tiling(i);
      for (int j = 0; j < std::size(mappable_format_list); ++j)
        result &= test_gem_map_format(j, i);
    }

    result &= test_dmabuf_map();
  }
  result &= test_destroy();

  if (!result) {
    printf("[  FAILED  ] graphics_Gbm test failed\n");
    return EXIT_FAILURE;
  }

  printf("[  PASSED  ] graphics_Gbm test success\n");
  return EXIT_SUCCESS;
}
