/*
 * Copyright 2018 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <drm_fourcc.h>
#include <stdint.h>
#include <stdio.h>
#include <xf86drmMode.h>
#include "bs_drm.h"

struct modifier_table_t {
	char *name;
	uint64_t value;
};

#define MODIFIER_FORMAT(f) \
	{                  \
#f, f      \
	}

static const struct modifier_table_t modifier_table[] = {
	MODIFIER_FORMAT(DRM_FORMAT_MOD_INVALID),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_LINEAR),
	MODIFIER_FORMAT(I915_FORMAT_MOD_X_TILED),
	MODIFIER_FORMAT(I915_FORMAT_MOD_Y_TILED),
	MODIFIER_FORMAT(I915_FORMAT_MOD_Yf_TILED),
	MODIFIER_FORMAT(I915_FORMAT_MOD_Y_TILED_CCS),
	MODIFIER_FORMAT(I915_FORMAT_MOD_Yf_TILED_CCS),
	MODIFIER_FORMAT(I915_FORMAT_MOD_4_TILED),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_SAMSUNG_64_32_TILE),
#ifdef DRM_FORMAT_MOD_QCOM_COMPRESSED
	MODIFIER_FORMAT(DRM_FORMAT_MOD_QCOM_COMPRESSED),
#endif
	MODIFIER_FORMAT(DRM_FORMAT_MOD_VIVANTE_TILED),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_VIVANTE_SUPER_TILED),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_VIVANTE_SPLIT_TILED),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_VIVANTE_SPLIT_SUPER_TILED),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_NVIDIA_TEGRA_TILED),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(0)),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(1)),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(2)),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(3)),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(4)),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_NVIDIA_16BX2_BLOCK(5)),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_BROADCOM_VC4_T_TILED),
	MODIFIER_FORMAT(DRM_FORMAT_MOD_ARM_AFBC(AFBC_FORMAT_MOD_BLOCK_SIZE_16x16|AFBC_FORMAT_MOD_SPARSE|AFBC_FORMAT_MOD_YTR)),
};
#undef MODIFIER_FORMAT

static char *modifier_to_string(uint64_t modifier)
{
	for (int i = 0; i < BS_ARRAY_LEN(modifier_table); i++) {
		if (modifier_table[i].value == modifier)
			return modifier_table[i].name;
	}
	return "UNKNOWN";
}

static void add_modifiers(int fd, uint32_t blob_id, uint64_t *modifier_list)
{
	drmModePropertyBlobPtr blob = drmModeGetPropertyBlob(fd, blob_id);
	if (blob) {
		struct drm_format_modifier_blob *header = blob->data;
		struct drm_format_modifier *modifiers;
		modifiers =
		    (struct drm_format_modifier *)((char *)header + header->modifiers_offset);
		for (int i = 0; i < header->count_modifiers; i++) {
			for (int j = 0; j < 64; j++) {
				if (modifier_list[j] == modifiers[i].modifier) {
					break;
				} else if (modifier_list[j] == 0) {
					modifier_list[j] = modifiers[i].modifier;
					break;
				}
			}
		}
	}
	drmModeFreePropertyBlob(blob);
}

void bs_print_supported_modifiers(int fd)
{
	uint64_t supported_modifier_list[64] = { 0 };
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
	drmModePlaneResPtr plane_res = drmModeGetPlaneResources(fd);
	if (plane_res) {
		for (int i = 0; i < plane_res->count_planes; i++) {
			drmModePlanePtr plane = drmModeGetPlane(fd, plane_res->planes[i]);
			if (plane) {
				drmModeObjectPropertiesPtr properties = drmModeObjectGetProperties(
				    fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
				if (properties) {
					for (int j = 0; j < properties->count_props; j++) {
						drmModePropertyPtr prop =
						    drmModeGetProperty(fd, properties->props[j]);
						if (strcmp(prop->name, "IN_FORMATS") == 0)
							add_modifiers(fd,
								      properties->prop_values[j],
								      supported_modifier_list);
						drmModeFreeProperty(prop);
					}
				}
				drmModeFreeObjectProperties(properties);
			}
			drmModeFreePlane(plane);
		}
		drmModeFreePlaneResources(plane_res);
	}
	printf("[ ");
	for (int i = 0; i < 64; i++) {
		if (supported_modifier_list[i] == 0)
			break;
		printf("%s%s", i ? ", " : "", modifier_to_string(supported_modifier_list[i]));
	}
	printf(" ]\n");
}

bool bs_are_modifier_supported(int fd, int drm_plane_type)
{
	bool modifiers_supported = false;
	drmSetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
	drmModePlaneResPtr plane_res = drmModeGetPlaneResources(fd);
	for (int i = 0; plane_res && i < plane_res->count_planes && !modifiers_supported; i++) {
		drmModePlanePtr plane = drmModeGetPlane(fd, plane_res->planes[i]);
		if (plane) {
			drmModeObjectPropertiesPtr properties =
			    drmModeObjectGetProperties(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE);
			for (int j = 0; properties && j < properties->count_props; j++) {
				drmModePropertyPtr prop =
				    drmModeGetProperty(fd, properties->props[j]);
				if (strcmp(prop->name, "type") == 0 &&
				    properties->prop_values[0] != drm_plane_type) {
					drmModeFreeProperty(prop);
					break;
				}
				if (strcmp(prop->name, "IN_FORMATS") == 0)
					modifiers_supported = true;
				drmModeFreeProperty(prop);
			}
			drmModeFreeObjectProperties(properties);
		}
		drmModeFreePlane(plane);
	}
	drmModeFreePlaneResources(plane_res);
	return modifiers_supported;
}

uint64_t bs_string_to_modifier(const char *modifier_str)
{
	for (int i = 0; i < BS_ARRAY_LEN(modifier_table); i++) {
		if (strcasecmp(modifier_str, modifier_table[i].name) == 0)
			return modifier_table[i].value;
	}
	return UINT64_MAX;
}

uint32_t bs_drm_find_property_id(int fd, uint32_t object_id, uint32_t object_type,
				 const char *property_name)
{
	drmModeObjectPropertiesPtr props = drmModeObjectGetProperties(fd, object_id, object_type);
	assert(props);
	uint32_t prop_id = 0;
	for (uint32_t i = 0; !prop_id && i < props->count_props; ++i) {
		drmModePropertyPtr prop = drmModeGetProperty(fd, props->props[i]);
		if (!strcmp(prop->name, property_name)) {
			prop_id = prop->prop_id;
		}
		drmModeFreeProperty(prop);
	}
	assert(prop_id);
	drmModeFreeObjectProperties(props);
	return prop_id;
}

unsigned int bs_get_crtc_bitmask(int fd, uint32_t crtc_id)
{
	drmModeResPtr mode_res = drmModeGetResources(fd);
	if (mode_res == NULL) {
		bs_debug_error("failed to get Mode Resources.");
		return -1;
	}
	unsigned int crtc_offset = 0;
	for (size_t i = 0; i < mode_res->count_crtcs; ++i) {
		if (mode_res->crtcs[i] == crtc_id) {
			crtc_offset = (1 << i);
			break;
		}
	}
	drmModeFreeResources(mode_res);
	return crtc_offset;
}

uint32_t bs_get_plane_id(int fd, uint32_t crtc_id)
{
	unsigned int crtc_offset = bs_get_crtc_bitmask(fd, crtc_id);
	if (crtc_offset < 1) {
		bs_debug_error("failed to get a valid CRTC bitmask.");
		return 0;
	}
	drmModePlaneResPtr plane_res = drmModeGetPlaneResources(fd);
	if (plane_res == NULL) {
		bs_debug_error("failed to get plane resources.");
		return 0;
	}
	uint32_t plane_id = 0;
	for (uint32_t plane_idx = 0; !plane_id && plane_idx < plane_res->count_planes;
	     plane_idx++) {
		drmModePlanePtr plane = drmModeGetPlane(fd, plane_res->planes[plane_idx]);
		if (plane == NULL)
			continue;
		if (plane->possible_crtcs & crtc_offset) {
			plane_id = plane_res->planes[plane_idx];
		}
		drmModeFreePlane(plane);
	}
	drmModeFreePlaneResources(plane_res);
	return plane_id;
}
