/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright (c) 2014 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 *
 * This is a test meant to exercise the VGEM DRM kernel module's PRIME
 * import/export functions. It will create a gem buffer object, mmap, write, and
 * then verify it. Then the test will repeat that with the same gem buffer, but
 * exported and then imported. Finally, a new gem buffer object is made in a
 * different driver which exports into VGEM and the mmap, write, verify sequence
 * is repeated on that.
 */

#include <cassert>
#include <fcntl.h>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <sys/mman.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <unistd.h>
#include <cerrno>
#include <xf86drm.h>
#include <linux/udmabuf.h>

extern "C" {
#include "bs_drm.h"
}

#define HANDLE_EINTR_AND_EAGAIN(x) \
	({							   \
		int result;				   \
		do {					   \
			result = (x);		   \
		} while (result != -1 && (errno == EINTR || errno == EAGAIN)); \
		result;					   \
	})

#define fail_if(cond, ...)		   \
	do {						   \
		if (cond) {				   \
			bs_debug_print("FAIL", __func__, __FILE__, __LINE__, __VA_ARGS__); \
			exit(EXIT_FAILURE);	   \
		}						   \
	} while (0)

constexpr uint32_t g_bo_pattern = 0xdeadbeef;

void *mmap_dumb_bo(const int fd, const int handle, const size_t size) {
	drm_mode_map_dumb mmap_arg{};

	memset(&mmap_arg, 0, sizeof(mmap_arg));

	mmap_arg.handle = handle;

	const int ret = drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &mmap_arg);
	assert(ret == 0);
	assert(mmap_arg.offset != 0);

	return mmap(nullptr, size, (PROT_READ | PROT_WRITE), MAP_SHARED, fd, static_cast<__off64_t>(mmap_arg.offset));
}

void write_pattern(uint32_t *bo_ptr, const size_t bo_size) {
	for (uint32_t *ptr = bo_ptr; ptr < bo_ptr + (bo_size / sizeof(*bo_ptr)); ptr++) {
		*ptr = g_bo_pattern;
	}
}

bool verify_pattern(const uint32_t *bo_ptr, const size_t bo_size) {
	for (const uint32_t *ptr = bo_ptr; ptr < bo_ptr + (bo_size / sizeof(*bo_ptr)); ptr++) {
		fail_if(*ptr != g_bo_pattern, "buffer object verify");
	}

	return true;
}

int create_udmabuf(const int fd, const size_t length) {
	const int udmabuf_dev_fd = HANDLE_EINTR_AND_EAGAIN(open("/dev/udmabuf", O_RDWR));
	fail_if(udmabuf_dev_fd < 0, "error opening /dev/udmabuf");

	udmabuf_create create{};
	create.memfd = fd;
	create.flags = UDMABUF_FLAGS_CLOEXEC;
	create.offset = 0;
	create.size = length;

	const int dmabuf_fd = HANDLE_EINTR_AND_EAGAIN(ioctl(udmabuf_dev_fd, UDMABUF_CREATE, &create));
	fail_if(dmabuf_fd < 0, "error creating udmabuf");

	close(udmabuf_dev_fd);
	return dmabuf_fd;
}

int create_memfd(const size_t length) {
	const int fd = memfd_create("test memfd", MFD_ALLOW_SEALING);
	fail_if(fd == -1, "memfd_create() error: %s", strerror(errno));

	const int res = HANDLE_EINTR_AND_EAGAIN(ftruncate(fd, length));
	fail_if(res == -1, "ftruncate() error: %s", strerror(errno));

	// udmabuf_create requires that file descriptors be sealed with
	// F_SEAL_SHRINK.
	fail_if(fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK) < 0,
	        "fcntl() error: %s", strerror(errno));

	return fd;
}

void test_import_dma_buf(gbm_device *gbm) {
	constexpr uint32_t width = 64;
	constexpr uint32_t height = 64;
	constexpr size_t size = width * height * 4;

	const int memfd_fd = create_memfd(size);
	fail_if(memfd_fd == -1, "failed to create memfd");

	auto *memfd_map =
			static_cast<uint32_t *>(mmap(nullptr, size, PROT_WRITE | PROT_READ, MAP_SHARED, memfd_fd, 0));
	fail_if(memfd_map == MAP_FAILED, "failed to mmap memfd");

	write_pattern(memfd_map, size);
	fail_if(!verify_pattern(memfd_map, size), "failed to verify pattern");

	fail_if(munmap(memfd_map, size) != 0, "munmap failed");

	int udmabuf_fd = create_udmabuf(memfd_fd, size);
	fail_if(udmabuf_fd == -1, "failed to create udmabuf");

	fail_if(close(memfd_fd) != 0, "close memfd failed");

	gbm_import_fd_modifier_data gbm_import_data = {
		.width = width,
		.height = height,
		.format = GBM_FORMAT_ARGB8888,
		.num_fds = 1,
		.fds = {udmabuf_fd},
		.strides = {static_cast<int>(width * 4)},
		.offsets = {},
		.modifier = DRM_FORMAT_MOD_LINEAR,
	};

	gbm_bo *bo = gbm_bo_import(gbm, GBM_BO_IMPORT_FD_MODIFIER,
	                           &gbm_import_data, GBM_BO_USE_RENDERING);
	fail_if(bo == nullptr, "failed to import bo");

	fail_if(close(udmabuf_fd) != 0, "failed to close udmabuf_fd");

	uint32_t stride;
	void *map_data;
	const uint32_t *bo_map = static_cast<uint32_t *>(gbm_bo_map(bo, 0, 0, width, height,
	                                                            GBM_BO_TRANSFER_READ, &stride, &map_data));

	fail_if(bo_map == MAP_FAILED, "gbm_bo_map failed");

	fail_if(!verify_pattern(bo_map, size), "pattern mismatch after gbm_bo_map");

	gbm_bo_unmap(bo, map_data);

	gbm_bo_destroy(bo);
}

void test_export_dma_buf(gbm_device *gbm) {
	constexpr uint32_t width = 64;
	constexpr uint32_t height = 64;

	gbm_bo *bo = gbm_bo_create(gbm, width, height,
	                           GBM_FORMAT_ARGB8888, GBM_BO_USE_LINEAR);
	fail_if(bo == nullptr, "create bo failed");

	uint32_t stride;
	void *map_data;
	auto *bo_map = static_cast<uint32_t *>(gbm_bo_map(bo, 0, 0, width, height,
	                                                  GBM_BO_TRANSFER_WRITE, &stride, &map_data));
	fail_if(bo_map == MAP_FAILED, "gbm_bo_map failed");

	fail_if(stride != gbm_bo_get_stride(bo),
	        "mapped buffer stride doesn't match bo stride");
	const uint32_t size = stride * height;
	write_pattern(bo_map, size);

	gbm_bo_unmap(bo, map_data);

	const int dmabuf_fd = gbm_bo_get_fd(bo);
	fail_if(dmabuf_fd == -1, "dma-buf export failed");

	auto *dmabuf_map =
			static_cast<uint32_t *>(mmap(nullptr, size, PROT_WRITE | PROT_READ, MAP_SHARED, dmabuf_fd, 0));
	fail_if(dmabuf_map == MAP_FAILED, "failed to mmap memfd");

	fail_if(!verify_pattern(dmabuf_map, size), "pattern mismatch after gbm_bo_map");

	fail_if(munmap(dmabuf_map, size) != 0, "munmap failed");
}

int main(int argc, char *argv[]) {
	const int fd = bs_drm_open_for_display();
	fail_if(fd == -1, "error opening dri card");

	gbm_device *gbm = gbm_create_device(fd);
	fail_if(gbm == nullptr, "failed to create gbm device");

	test_import_dma_buf(gbm);

	test_export_dma_buf(gbm);

	gbm_device_destroy(gbm);
	close(fd);

	return EXIT_SUCCESS;
}
