/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <memory>

#include <vulkan/vulkan.h>

extern "C" {
#include "bs_drm.h"
}

// Used for double-buffering.
struct frame {
	gbm_bo *bo;
	uint32_t drm_fb_id;
	VkDeviceMemory vk_memory;
	VkImage vk_image;
	VkImageView vk_image_view;
	VkFramebuffer vk_framebuffer;
	VkCommandBuffer vk_cmd_buf;
};

#define CHECK_VK_SUCCESS(result, vk_func) \
	check_vk_success(__FILE__, __LINE__, __func__, (result), (vk_func))

static void check_vk_success(const char *file, const int line, const char *func, const VkResult result,
                             const char *vk_func) {
	if (result == VK_SUCCESS)
		return;

	bs_debug_print("ERROR", func, file, line, "%s failed with VkResult(%d)", vk_func, result);
	exit(EXIT_FAILURE);
}

static void page_flip_handler(int /* fd */, unsigned int /* frame */, unsigned int /* sec */, unsigned int /* usec */,
                              void *data) {
	const auto waiting_for_flip = static_cast<bool *>(data);
	*waiting_for_flip = false;
}

// Add child to the pNext chain of parent.
static void chain_add(void *parent, void *child) {
	auto *p = static_cast<VkBaseOutStructure *>(parent);
	auto *c = static_cast<VkBaseOutStructure *>(child);

	c->pNext = p->pNext;
	p->pNext = c;
}

// Choose a physical device that supports Vulkan 1.1 or later. Exit on failure.
VkPhysicalDevice choose_physical_device(VkInstance inst) {
	uint32_t n_phys_devs;
	std::unique_ptr<VkPhysicalDevice[]> phys_devs;

	VkResult res = vkEnumeratePhysicalDevices(inst, &n_phys_devs, nullptr);
	CHECK_VK_SUCCESS(res, "vkEnumeratePhysicalDevices");

	if (n_phys_devs == 0) {
		fprintf(stderr, "No available VkPhysicalDevices\n");
		exit(EXIT_FAILURE);
	}

	phys_devs = std::make_unique<VkPhysicalDevice[]>(n_phys_devs);
	res = vkEnumeratePhysicalDevices(inst, &n_phys_devs, phys_devs.get());
	CHECK_VK_SUCCESS(res, "vkEnumeratePhysicalDevices");

	// Print information about all available devices. This helps debugging
	// when bringing up Vulkan on a new system.
	printf("Available VkPhysicalDevices:\n");

	uint32_t physical_device_idx = 0;
	VkPhysicalDevice physical_device = VK_NULL_HANDLE;
	for (uint32_t i = 0; i < n_phys_devs; ++i) {
		VkPhysicalDeviceProperties props;

		vkGetPhysicalDeviceProperties(phys_devs[i], &props);

		printf("    VkPhysicalDevice %u:\n", i);
		printf("	apiVersion: %u.%u.%u\n", VK_VERSION_MAJOR(props.apiVersion),
		       VK_VERSION_MINOR(props.apiVersion), VK_VERSION_PATCH(props.apiVersion));
		printf("	driverVersion: %u\n", props.driverVersion);
		printf("	vendorID: 0x%x\n", props.vendorID);
		printf("	deviceID: 0x%x\n", props.deviceID);
		printf("	deviceName: %s\n", props.deviceName);
		printf("	pipelineCacheUUID: %x%x%x%x-%x%x-%x%x-%x%x-%x%x%x%x%x%x\n",
		       props.pipelineCacheUUID[0], props.pipelineCacheUUID[1],
		       props.pipelineCacheUUID[2], props.pipelineCacheUUID[3],
		       props.pipelineCacheUUID[4], props.pipelineCacheUUID[5],
		       props.pipelineCacheUUID[6], props.pipelineCacheUUID[7],
		       props.pipelineCacheUUID[8], props.pipelineCacheUUID[9],
		       props.pipelineCacheUUID[10], props.pipelineCacheUUID[11],
		       props.pipelineCacheUUID[12], props.pipelineCacheUUID[13],
		       props.pipelineCacheUUID[14], props.pipelineCacheUUID[15]);
		if (physical_device == VK_NULL_HANDLE &&
		    VK_VERSION_MAJOR(props.apiVersion) >= 1 &&
		    VK_VERSION_MINOR(props.apiVersion) >= 1) {
			physical_device_idx = i;
			physical_device = phys_devs[i];
		}
	}
	phys_devs.reset();

	if (physical_device == VK_NULL_HANDLE) {
		bs_debug_error("unable to find a suitable physical device");
		exit(EXIT_FAILURE);
	}
	printf("Chose VkPhysicalDevice %d\n", physical_device_idx);
	fflush(stdout);
	return physical_device;
}

// Return the index of a graphics-enabled queue family. Return UINT32_MAX on
// failure.
uint32_t choose_gfx_queue_family(VkPhysicalDevice phys_dev) {
	uint32_t family_idx = UINT32_MAX;
	std::unique_ptr<VkQueueFamilyProperties[]> props;
	uint32_t n_props = 0;

	vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &n_props, nullptr);
	props = std::make_unique<VkQueueFamilyProperties[]>(n_props);
	vkGetPhysicalDeviceQueueFamilyProperties(phys_dev, &n_props, props.get());

	// Choose the first graphics queue.
	for (uint32_t i = 0; i < n_props; ++i) {
		if ((props[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) && props[i].queueCount > 0) {
			family_idx = i;
			break;
		}
	}

	props.reset();
	return family_idx;
}

// Fail the test if the image is unsupported.
static void require_image_support(VkPhysicalDevice phys_dev, const VkFormat format,
                                  const uint64_t drm_format_mod, const uint32_t width, const uint32_t height) {
	VkFormatProperties2 format_props = {
		.sType = VK_STRUCTURE_TYPE_FORMAT_PROPERTIES_2,
	};

	VkDrmFormatModifierPropertiesListEXT mod_props_list = {
		.sType = VK_STRUCTURE_TYPE_DRM_FORMAT_MODIFIER_PROPERTIES_LIST_EXT,
	};

	chain_add(&format_props, &mod_props_list);

	vkGetPhysicalDeviceFormatProperties2(phys_dev, format, &format_props);

	if (mod_props_list.drmFormatModifierCount == 0) {
		bs_debug_error("Vulkan does not support VkFormat(%d), "
		               "drmFormatModifier=0x%08luX", format, drm_format_mod);
		exit(EXIT_FAILURE);
	}

	mod_props_list.pDrmFormatModifierProperties = static_cast<VkDrmFormatModifierPropertiesEXT *>(alloca(
		mod_props_list.drmFormatModifierCount *
		sizeof(mod_props_list.pDrmFormatModifierProperties[0])));

	vkGetPhysicalDeviceFormatProperties2(phys_dev, format, &format_props);

	const VkDrmFormatModifierPropertiesEXT *mod_props = nullptr;

	for (uint32_t i = 0; i < mod_props_list.drmFormatModifierCount; ++i) {
		const VkDrmFormatModifierPropertiesEXT *tmp_props =
				&mod_props_list.pDrmFormatModifierProperties[i];

		if (tmp_props->drmFormatModifier == drm_format_mod) {
			mod_props = tmp_props;
			break;
		}
	}

	if (!mod_props) {
		bs_debug_error("Vulkan does not support VkFormat(%d), "
		               "drmFormatModifier=0x%08luX", format, drm_format_mod);
		exit(EXIT_FAILURE);
	}

	if (!(mod_props->drmFormatModifierTilingFeatures &
	      VK_FORMAT_FEATURE_COLOR_ATTACHMENT_BIT)) {
		bs_debug_error("Vulkan supports VkFormat(%d), drmFormatModifier=0x%08luX but lacks required features", format,
		               drm_format_mod);
		exit(EXIT_FAILURE);
	}

	if (mod_props->drmFormatModifierPlaneCount != 1) {
		bs_debug_error("FINISHME: support drmFormatModifierPlaneCount > 1");
		exit(EXIT_FAILURE);
	}

	VkPhysicalDeviceImageFormatInfo2 image_format_info2 = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2,
		.format = format,
		.type = VK_IMAGE_TYPE_2D,
		.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
	};

	VkPhysicalDeviceImageDrmFormatModifierInfoEXT image_mod_info = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_DRM_FORMAT_MODIFIER_INFO_EXT,
		.drmFormatModifier = drm_format_mod,
	};

	chain_add(&image_format_info2, &image_mod_info);

	VkPhysicalDeviceExternalImageFormatInfo external_image_format_info = {
		.sType = VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_EXTERNAL_IMAGE_FORMAT_INFO,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};

	chain_add(&image_format_info2, &external_image_format_info);

	VkImageFormatProperties2 image_format_props2 = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2,
	};

	const VkImageFormatProperties *image_format_props = &image_format_props2.imageFormatProperties;

	VkExternalImageFormatProperties external_image_format_props = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_IMAGE_FORMAT_PROPERTIES,
	};

	chain_add(&image_format_props2, &external_image_format_props);

	const VkResult res = vkGetPhysicalDeviceImageFormatProperties2(phys_dev, &image_format_info2,
	                                                               &image_format_props2);

	if (res == VK_ERROR_FORMAT_NOT_SUPPORTED) {
		bs_debug_error("vkGetPhysicalDeviceFormatProperties2 does not support image");
		exit(EXIT_FAILURE);
	}

	CHECK_VK_SUCCESS(res, "vkGetPhysicalDeviceFormatProperties2");

	if (image_format_props->maxExtent.width < width ||
	    image_format_props->maxExtent.height < height ||
	    !(image_format_props->sampleCounts & VK_SAMPLE_COUNT_1_BIT) ||
	    !(external_image_format_props.externalMemoryProperties.externalMemoryFeatures &
	      VK_EXTERNAL_MEMORY_FEATURE_IMPORTABLE_BIT)) {
		bs_debug_error("vkGetPhysicalDeviceFormatProperties2 does not support image");
		exit(EXIT_FAILURE);
	}

	if (external_image_format_props.externalMemoryProperties.externalMemoryFeatures &
	    VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT) {
		bs_debug_error("FINISHME: support VK_EXTERNAL_MEMORY_FEATURE_DEDICATED_ONLY_BIT");
		exit(EXIT_FAILURE);
	}
}

VkImage create_image(VkPhysicalDevice phys_dev, VkDevice dev, struct gbm_bo *bo, VkFormat format,
                     const uint64_t drm_format_mod) {
	VkImage image;

	VkImageCreateInfo base_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO,
		.imageType = VK_IMAGE_TYPE_2D,
		.format = format,
		.extent = (VkExtent3D){
			gbm_bo_get_width(bo),
			gbm_bo_get_height(bo), 1
		},
		.mipLevels = 1,
		.arrayLayers = 1,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.tiling = VK_IMAGE_TILING_DRM_FORMAT_MODIFIER_EXT,
		.usage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
	};

	VkSubresourceLayout layout = {
		.offset = gbm_bo_get_offset(bo, 0),
		.size = 0, // ignored
		.rowPitch = gbm_bo_get_stride_for_plane(bo, 0),
	};

	VkImageDrmFormatModifierExplicitCreateInfoEXT mod_create_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_DRM_FORMAT_MODIFIER_EXPLICIT_CREATE_INFO_EXT,
		.drmFormatModifier = drm_format_mod,
		.drmFormatModifierPlaneCount = 1,
		.pPlaneLayouts = &layout,
	};

	chain_add(&base_create_info, &mod_create_info);

	VkExternalMemoryImageCreateInfo external_create_info = {
		.sType = VK_STRUCTURE_TYPE_EXTERNAL_MEMORY_IMAGE_CREATE_INFO,
		.handleTypes = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	};

	chain_add(&base_create_info, &external_create_info);

	const VkResult res = vkCreateImage(dev, &base_create_info, nullptr, &image);
	CHECK_VK_SUCCESS(res, "vkCreateImage");

	return image;
}

bool bind_image_bo(VkDevice dev,
                   VkImage image,
                   gbm_bo *bo,
                   VkDeviceMemory *mems) {
	const auto GetMemoryFdPropertiesKHR = reinterpret_cast<PFN_vkGetMemoryFdPropertiesKHR>(
		vkGetDeviceProcAddr(dev, "vkGetMemoryFdPropertiesKHR"));
	if (GetMemoryFdPropertiesKHR == nullptr) {
		bs_debug_error("vkGetDeviceProcAddr(\"vkGetMemoryFdPropertiesKHR\") failed");
		return false;
	}

	const int prime_fd = gbm_bo_get_fd(bo);
	if (prime_fd < 0) {
		bs_debug_error("failed to get prime fd for gbm_bo");
		return false;
	}

	VkMemoryFdPropertiesKHR fd_props = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_FD_PROPERTIES_KHR,
	};

	GetMemoryFdPropertiesKHR(dev,
	                         VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
	                         prime_fd, &fd_props);

	/* get image memory requirements */
	const VkImageMemoryRequirementsInfo2 mem_reqs_info = {
		.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_REQUIREMENTS_INFO_2,
		.image = image,
	};

	VkMemoryRequirements2 mem_reqs = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_REQUIREMENTS_2,
	};

	vkGetImageMemoryRequirements2(dev, &mem_reqs_info, &mem_reqs);

	const uint32_t memory_type_bits = fd_props.memoryTypeBits &
	                                  mem_reqs.memoryRequirements.memoryTypeBits;

	if (!memory_type_bits) {
		bs_debug_error("no valid memory type");
		close(prime_fd);
		return false;
	}

	const VkMemoryDedicatedAllocateInfo memory_dedicated_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_DEDICATED_ALLOCATE_INFO,
		.image = image,
	};

	const VkImportMemoryFdInfoKHR memory_fd_info = {
		.sType = VK_STRUCTURE_TYPE_IMPORT_MEMORY_FD_INFO_KHR,
		.pNext = &memory_dedicated_info,
		.handleType = VK_EXTERNAL_MEMORY_HANDLE_TYPE_DMA_BUF_BIT_EXT,
		.fd = prime_fd,
	};

	const VkMemoryAllocateInfo memory_info = {
		.sType = VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO,
		.pNext = &memory_fd_info,
		.allocationSize = mem_reqs.memoryRequirements.size,
		.memoryTypeIndex = static_cast<uint32_t>(ffs(static_cast<int>(memory_type_bits)) - 1),
	};

	VkResult res = vkAllocateMemory(dev, &memory_info, nullptr, mems);
	CHECK_VK_SUCCESS(res, "vkAllocateMemory");

	res = vkBindImageMemory(dev, image, mems[0], 0);
	CHECK_VK_SUCCESS(res, "vkBindImageMemory");

	return true;
}

int main(int /* argc */, char ** /* argv */) {
	constexpr uint32_t drm_format = DRM_FORMAT_XBGR8888;
	constexpr VkFormat format = VK_FORMAT_A8B8G8R8_UNORM_PACK32;
	constexpr uint64_t drm_format_mod = DRM_FORMAT_MOD_LINEAR;
	bs_debug_warning("assume display supports DRM_FORMAT_XBGR8888 without querying plane "
		"properties");

	VkInstance inst;
	VkDevice dev;
	VkQueue gfx_queue;
	VkRenderPass pass;
	VkCommandPool cmd_pool;
	frame frames[2];

	const int dev_fd = bs_drm_open_main_display();
	if (dev_fd < 0) {
		bs_debug_error("failed to open display device");
		exit(EXIT_FAILURE);
	}

	gbm_device *gbm = gbm_create_device(dev_fd);
	if (!gbm) {
		bs_debug_error("failed to create gbm_device");
		exit(EXIT_FAILURE);
	}

	bs_drm_pipe pipe{};
	if (!bs_drm_pipe_make(dev_fd, &pipe)) {
		bs_debug_error("failed to make drm pipe");
		exit(EXIT_FAILURE);
	}

	const drmModeConnector *connector = drmModeGetConnector(dev_fd, pipe.connector_id);
	if (!connector) {
		bs_debug_error("drmModeGetConnector failed");
		exit(EXIT_FAILURE);
	}

	drmModeModeInfo *mode = &connector->modes[0];
	VkApplicationInfo app_info = {
		.sType = VK_STRUCTURE_TYPE_APPLICATION_INFO,
		.apiVersion = VK_MAKE_VERSION(1, 1, 0),
	};
	VkInstanceCreateInfo inst_info = {
		.sType = VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO,
		.pApplicationInfo = &app_info,
	};
	VkResult res = vkCreateInstance(&inst_info, nullptr, &inst);
	CHECK_VK_SUCCESS(res, "vkCreateInstance");

	VkPhysicalDevice phys_dev = choose_physical_device(inst);

	const uint32_t gfx_queue_family_idx = choose_gfx_queue_family(phys_dev);
	if (gfx_queue_family_idx == UINT32_MAX) {
		bs_debug_error(
			"VkPhysicalDevice exposes no VkQueueFamilyProperties "
			"with graphics");
		exit(EXIT_FAILURE);
	}

	const char *required_extensions[] = {
		"VK_KHR_external_memory_fd",
		"VK_KHR_image_format_list",
		"VK_EXT_external_memory_dma_buf",
		"VK_EXT_image_drm_format_modifier",
		"VK_EXT_queue_family_foreign",
	};
	uint32_t extension_count;
	std::unique_ptr<VkExtensionProperties[]> available_extensions;
	vkEnumerateDeviceExtensionProperties(phys_dev, nullptr, &extension_count, nullptr);
	available_extensions = std::make_unique<VkExtensionProperties[]>(extension_count);
	vkEnumerateDeviceExtensionProperties(phys_dev, nullptr, &extension_count, available_extensions.get());
	for (auto &required_extension: required_extensions) {
		uint32_t j;
		for (j = 0; j < extension_count; j++) {
			if (strcmp(required_extension, available_extensions[j].extensionName) == 0) {
				break;
			}
		}
		if (j == extension_count) {
			bs_debug_error("unsupported device extension: %s", required_extension);
			exit(EXIT_FAILURE);
		}
	}
	available_extensions.reset();

	require_image_support(phys_dev, format, drm_format_mod, mode->hdisplay, mode->vdisplay);

	float queue_priorities = 1.0f;

	VkDeviceQueueCreateInfo queue_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO,
		.queueFamilyIndex = gfx_queue_family_idx,
		.queueCount = 1,
		.pQueuePriorities = &queue_priorities,
	};

	VkDeviceCreateInfo dev_info = {
		.sType = VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO,
		.queueCreateInfoCount = 1,
		.pQueueCreateInfos = &queue_info,
		.enabledExtensionCount = BS_ARRAY_LEN(required_extensions),
		.ppEnabledExtensionNames = required_extensions,
	};

	res = vkCreateDevice(phys_dev, &dev_info, nullptr, &dev);
	CHECK_VK_SUCCESS(res, "vkCreateDevice");

	vkGetDeviceQueue(dev, gfx_queue_family_idx, /*queueIndex*/ 0, &gfx_queue);

	VkCommandPoolCreateInfo cmd_pool_info = {
		.sType = VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO,
		.flags = VK_COMMAND_POOL_CREATE_TRANSIENT_BIT |
		         VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT,
		.queueFamilyIndex = gfx_queue_family_idx,
	};

	res = vkCreateCommandPool(dev, &cmd_pool_info, nullptr, &cmd_pool);
	CHECK_VK_SUCCESS(res, "vkCreateCommandPool");

	VkAttachmentDescription color_attachment = {
		.format = format,
		.samples = VK_SAMPLE_COUNT_1_BIT,
		.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR,
		.storeOp = VK_ATTACHMENT_STORE_OP_STORE,
		.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED,
		.finalLayout = VK_IMAGE_LAYOUT_GENERAL,
	};

	VkAttachmentReference color_attachment_ref = {
		.attachment = 0,
		.layout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL,
	};

	VkSubpassDescription subpass = {
		.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS,
		.colorAttachmentCount = 1,
		.pColorAttachments = &color_attachment_ref,
	};

	VkRenderPassCreateInfo render_pass_info = {
		.sType = VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO,
		.attachmentCount = 1,
		.pAttachments = &color_attachment,
		.subpassCount = 1,
		.pSubpasses = &subpass,
	};

	res = vkCreateRenderPass(dev, &render_pass_info, nullptr, &pass);
	CHECK_VK_SUCCESS(res, "vkCreateRenderPass");

	for (auto &i: frames) {
		frame *fr = &i;

		fr->bo = gbm_bo_create_with_modifiers(gbm, mode->hdisplay, mode->vdisplay,
		                                      drm_format, &drm_format_mod, 1);
		if (fr->bo == nullptr) {
			bs_debug_error("failed to create framebuffer's gbm_bo");
			return 1;
		}

		fr->drm_fb_id = bs_drm_fb_create_gbm(fr->bo);
		if (fr->drm_fb_id == 0) {
			bs_debug_error("failed to create drm framebuffer id");
			return 1;
		}

		fr->vk_image = create_image(phys_dev, dev, fr->bo, format, drm_format_mod);
		if (fr->vk_image == VK_NULL_HANDLE) {
			bs_debug_error("failed to create VkImage");
			exit(EXIT_FAILURE);
		}

		if (!bind_image_bo(dev, fr->vk_image, fr->bo, &fr->vk_memory)) {
			bs_debug_error("failed to bind bo to image");
			exit(EXIT_FAILURE);
		}

		VkImageViewCreateInfo image_view_info = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO,
			.image = fr->vk_image,
			.viewType = VK_IMAGE_VIEW_TYPE_2D,
			.format = format,
			.components = {
				.r = VK_COMPONENT_SWIZZLE_IDENTITY,
				.g = VK_COMPONENT_SWIZZLE_IDENTITY,
				.b = VK_COMPONENT_SWIZZLE_IDENTITY,
				.a = VK_COMPONENT_SWIZZLE_IDENTITY,
			},
			.subresourceRange = {
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			},
		};

		res = vkCreateImageView(dev, &image_view_info, nullptr, &fr->vk_image_view);
		CHECK_VK_SUCCESS(res, "vkCreateImageView");

		VkFramebufferCreateInfo framebuffer_info = {
			.sType = VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO,
			.renderPass = pass,
			.attachmentCount = 1,
			.pAttachments = &fr->vk_image_view,
			.width = mode->hdisplay,
			.height = mode->vdisplay,
			.layers = 1,
		};

		res = vkCreateFramebuffer(dev, &framebuffer_info, nullptr, &fr->vk_framebuffer);
		CHECK_VK_SUCCESS(res, "vkCreateFramebuffer");

		VkCommandBufferAllocateInfo cmd_buf_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO,
			.commandPool = cmd_pool,
			.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY,
			.commandBufferCount = 1,
		};

		res = vkAllocateCommandBuffers(dev, &cmd_buf_info, &fr->vk_cmd_buf);
		CHECK_VK_SUCCESS(res, "vkAllocateCommandBuffers");
	}

	// We set the screen mode using framebuffer 0. Then the first page flip
	// waits on framebuffer 1.
	int err = drmModeSetCrtc(dev_fd, pipe.crtc_id, frames[0].drm_fb_id, 0, 0, &pipe.connector_id, 1, mode);
	if (err) {
		bs_debug_error("drmModeSetCrtc failed: %d", err);
		exit(EXIT_FAILURE);
	}

	// We set an upper bound on the render loop so we can run this in
	// from a testsuite.
	for (int i = 1; i < 500; ++i) {
		const frame *fr = &frames[i % BS_ARRAY_LEN(frames)];

		// vkBeginCommandBuffer implicitly resets the command buffer due
		// to VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT.
		VkCommandBufferBeginInfo begin_info = {
			.sType = VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO,
			.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT,
		};

		res = vkBeginCommandBuffer(fr->vk_cmd_buf, &begin_info);
		CHECK_VK_SUCCESS(res, "vkBeginCommandBuffer");

		VkImageMemoryBarrier memory_barrier = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = 0, /** ignored for transfers */
			.dstAccessMask = 0, /** ignored for transfers */
			.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.newLayout = VK_IMAGE_LAYOUT_UNDEFINED,
			.srcQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
			.dstQueueFamilyIndex = gfx_queue_family_idx,
			.image = fr->vk_image,
			.subresourceRange = (VkImageSubresourceRange){
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		};

		// Transfer ownership of the dma-buf from DRM to Vulkan.
		vkCmdPipelineBarrier(
			fr->vk_cmd_buf,
			// srcStageMask is ignored when acquiring ownership, but various
			// validation layers complain when you pass 0 here, so we just set
			// it to the same as dstStageMask.
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr,
			1,
			&memory_barrier);

		// Cycle along the circumference of the RGB color wheel.
		const VkClearValue clear_color = {
			.color =
			{
				.float32 =
				{
					0.5f + 0.5f * sinf(2 * M_PIf * static_cast<float>(i) / 240.0f),
					0.5f + 0.5f * sinf(2 * M_PIf * static_cast<float>(i) / 240.0f +
					                   (2.0f / 3.0f * M_PIf)),
					0.5f + 0.5f * sinf(2 * M_PIf * static_cast<float>(i) / 240.0f +
					                   (4.0f / 3.0f * M_PIf)),
					1.0f,
				},
			},
		};

		VkRenderPassBeginInfo render_pass_begin_info = {
			.sType = VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO,
			.renderPass = pass,
			.framebuffer = fr->vk_framebuffer,
			.renderArea = {
				.offset = {0, 0},
				.extent = {mode->hdisplay, mode->vdisplay},
			},
			.clearValueCount = 1,
			.pClearValues = &clear_color,
		};

		vkCmdBeginRenderPass(fr->vk_cmd_buf, &render_pass_begin_info, VK_SUBPASS_CONTENTS_INLINE);
		vkCmdEndRenderPass(fr->vk_cmd_buf);

		VkImageMemoryBarrier memory_barrier2 = {
			.sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER,
			.pNext = nullptr,
			.srcAccessMask = 0, /** ignored for transfers */
			.dstAccessMask = 0, /** ignored for transfers */
			.oldLayout = VK_IMAGE_LAYOUT_GENERAL,
			.newLayout = VK_IMAGE_LAYOUT_GENERAL,
			.srcQueueFamilyIndex = gfx_queue_family_idx,
			.dstQueueFamilyIndex = VK_QUEUE_FAMILY_FOREIGN_EXT,
			.image = fr->vk_image,
			.subresourceRange = (VkImageSubresourceRange){
				.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT,
				.baseMipLevel = 0,
				.levelCount = 1,
				.baseArrayLayer = 0,
				.layerCount = 1,
			}
		};

		// Transfer ownership of the dma-buf from Vulkan to DRM.
		vkCmdPipelineBarrier(
			fr->vk_cmd_buf,
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			// dstStageMask is ignored when releasing ownership, but various
			// validation layers complain when you pass 0 here, so we just set
			// it to the same as srcStageMask.
			VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT,
			0, 0, nullptr, 0, nullptr,
			1,
			&memory_barrier2);

		res = vkEndCommandBuffer(fr->vk_cmd_buf);
		CHECK_VK_SUCCESS(res, "vkEndCommandBuffer");

		VkSubmitInfo submit_info = {
			.sType = VK_STRUCTURE_TYPE_SUBMIT_INFO,
			.commandBufferCount = 1,
			.pCommandBuffers = &fr->vk_cmd_buf,
		};

		res = vkQueueSubmit(gfx_queue, 1, &submit_info, VK_NULL_HANDLE);
		CHECK_VK_SUCCESS(res, "vkQueueSubmit");

		res = vkQueueWaitIdle(gfx_queue);
		CHECK_VK_SUCCESS(res, "vkQueueWaitIdle");

		bool waiting_for_flip = true;
		err = drmModePageFlip(dev_fd, pipe.crtc_id, fr->drm_fb_id, DRM_MODE_PAGE_FLIP_EVENT,
		                      &waiting_for_flip);
		if (err) {
			bs_debug_error("failed page flip: error=%d", err);
			exit(EXIT_FAILURE);
		}

		while (waiting_for_flip) {
			drmEventContext ev_ctx = {
				.version = DRM_EVENT_CONTEXT_VERSION,
				.page_flip_handler = page_flip_handler,
			};

			fd_set fds;
			FD_ZERO(&fds);
			FD_SET(dev_fd, &fds);

			const int n_fds = select(dev_fd + 1, &fds, nullptr, nullptr, nullptr);
			if (n_fds < 0) {
				bs_debug_error("select() failed on page flip: %s", strerror(errno));
				exit(EXIT_FAILURE);
			}
			if (n_fds == 0) {
				bs_debug_error("select() timeout on page flip");
				exit(EXIT_FAILURE);
			}

			err = drmHandleEvent(dev_fd, &ev_ctx);
			if (err) {
				bs_debug_error(
					"drmHandleEvent failed while "
					"waiting for page flip: error=%d",
					err);
				exit(EXIT_FAILURE);
			}
		}
	}

	return 0;
}
