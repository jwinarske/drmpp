#ifndef DRMPP_EXAMPLES_VULKAN_VULKAN_KMS_H_
#define DRMPP_EXAMPLES_VULKAN_VULKAN_KMS_H_

#include <xf86drm.h>

#include "drmpp/vulkan/vulkan_base.h"

#include "drmpp/logging/logging.h"
#include "kms/device.h"

namespace drmpp::vulkan {

class VulkanKms : public VulkanBase {
 public:
  explicit VulkanKms(bool enable_validation_layers,
                     const vk::ApplicationInfo& application_info,
                     const std::vector<const char*>& requested_extensions = {});

  ~VulkanKms() override;

  virtual vk::ResultValue<std::shared_ptr<KmsDevice>> SelectKmsDevice(
      const std::string& device) = 0;

  virtual vk::ResultValue<const vk::PhysicalDevice> SelectPhysicalDevice(
      std::shared_ptr<KmsDevice> device,
      const vk::Instance& instance) = 0;

  virtual vk::ResultValue<std::pair<vk::DisplayKHR, vk::SurfaceKHR>>
  CreateDisplaySurface(const vk::Instance& instance,
                       const vk::PhysicalDevice& physical_device) = 0;

  virtual vk::ResultValue<std::pair<vk::Device, vk::PhysicalDevice>>
  InitializeDeviceKms(const vk::Instance& instance) = 0;

  virtual vk::Result InitializePhysicalDeviceKms(
      const vk::Instance& instance,
      const vk::PhysicalDevice& physical_device) {
    return vk::Result::eNotReady;
  }

  vk::Result InitializeVulkanKms(const std::string& device,
                                 bool protected_chain);

  [[nodiscard]] bool VulkanIsProtectedKms() const { return protected_; }

  void setVulkanDeviceKms(const vk::Device device) { device_ = device; }

  void setVulkanPhysicalDeviceKms(const vk::PhysicalDevice physical_device) {
    physical_device_ = physical_device;
  }

  [[nodiscard]] virtual bool run() const;

  static void CheckVkResult(VkResult err);

  [[nodiscard]] vk::SurfaceKHR getParentSurface() const {
    return parent_surface_;
  }

  static bool HasExtensionProperty(
      const std::vector<vk::ExtensionProperties>& properties,
      const char* req);

  static bool BussesMatch(drmDevicePtr drm_device,
                          vk::PhysicalDevice physical_device);

 private:
  bool protected_{};
  vk::Device device_;
  vk::SurfaceKHR parent_surface_;
  vk::PhysicalDevice physical_device_;
  vk::DisplayKHR display_;
};
}  // namespace drmpp::vulkan

#endif  // DRMPP_EXAMPLES_VULKAN_VULKAN_KMS_H_