
#include "drmpp/vulkan/vulkan_khr.h"

#include "drmpp/logging/logging.h"

namespace drmpp::vulkan {

VulkanKhr::VulkanKhr(const bool enable_validation_layers,
                     const vk::ApplicationInfo& application_info,
                     const std::vector<const char*>& requested_extensions)
    : VulkanBase(VK_KHR_DISPLAY_EXTENSION_NAME,
                 enable_validation_layers,
                 application_info,
                 requested_extensions) {}

VulkanKhr::~VulkanKhr() = default;

vk::Result VulkanKhr::InitializeVulkanKHR(const int gpu_number,
                                          const bool protected_chain) {
  const auto results = SelectPhysicalDevice(gpu_number, getVulkanInstance());
  CHECK_VK_RESULT(results.result);
  physical_device_ = results.value;

  const auto display_and_surface =
      CreateDisplaySurface(getVulkanInstance(), physical_device_);
  if (display_and_surface.result != vk::Result::eSuccess) {
    return display_and_surface.result;
  }
  display_ = display_and_surface.value.first;
  parent_surface_ = display_and_surface.value.second;

  if (protected_chain) {
    vk::PhysicalDeviceProtectedMemoryFeatures protected_features;
    vk::PhysicalDeviceFeatures2 features;
    features.pNext = &protected_features;
    physical_device_.getFeatures2(&features);

    if (!protected_features.protectedMemory) {
      LOG_WARN("Protected memory requested but not supported by device");
    }
    protected_ = protected_features.protectedMemory;
  }

  return vk::Result::eSuccess;
}

bool VulkanKhr::run() const {
  return true;
}

void VulkanKhr::CheckVkResult(const VkResult err) {
  if (err == 0)
    return;
  LOG_ERROR("[VulkanKhr] Error: VkResult = {}", static_cast<int>(err));
  if (err < 0)
    abort();
}

}  // namespace drmpp::vulkan
