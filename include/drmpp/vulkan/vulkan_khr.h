#ifndef DRMPP_EXAMPLES_VULKAN_VULKAN_KHR_H_
#define DRMPP_EXAMPLES_VULKAN_VULKAN_KHR_H_

#include "drmpp/vulkan/vulkan_base.h"

#include "drmpp/logging/logging.h"

namespace drmpp::vulkan {

class VulkanKhr : public VulkanBase {
 public:
  explicit VulkanKhr(bool enable_validation_layers,
                     const vk::ApplicationInfo& application_info,
                     const std::vector<const char*>& requested_extensions = {});

  ~VulkanKhr() override;

  virtual vk::ResultValue<vk::PhysicalDevice> SelectPhysicalDevice(
      int gpu_number,
      const vk::Instance& instance) = 0;

  virtual vk::ResultValue<std::pair<vk::DisplayKHR, vk::SurfaceKHR>>
  CreateDisplaySurface(const vk::Instance& instance,
                       const vk::PhysicalDevice& physical_device) = 0;

  virtual vk::ResultValue<std::pair<vk::Device, vk::PhysicalDevice>>
  InitializeDeviceKHR(const vk::Instance& instance) = 0;

  virtual vk::Result InitializePhysicalDevice2KHR(
      const vk::Instance& instance,
      const vk::PhysicalDevice& physical_device) {
    return vk::Result::eNotReady;
  }

  virtual vk::Result InitializePhysicalDeviceKHR(
      const vk::Instance& instance,
      const vk::PhysicalDevice& physical_device) {
    return vk::Result::eNotReady;
  }

  vk::Result InitializeVulkanKHR(int gpu_number, bool protected_chain);

  [[nodiscard]] bool VulkanIsProtectedKHR() const { return protected_; }

  void setVulkanDeviceKHR(const vk::Device device) { device_ = device; }

  void setVulkanPhysicalDeviceKHR(const vk::PhysicalDevice physical_device) {
    physical_device_ = physical_device;
  }

  [[nodiscard]] virtual bool run() const;

  static void CheckVkResult(VkResult err);

  [[nodiscard]] vk::SurfaceKHR getParentSurface() const {
    return parent_surface_;
  }

 private:
  bool protected_{};
  vk::Device device_;
  vk::SurfaceKHR parent_surface_;
  vk::PhysicalDevice physical_device_;
  vk::DisplayKHR display_;
};
}  // namespace drmpp::vulkan

#endif  // DRMPP_EXAMPLES_VULKAN_VULKAN_KHR_H_