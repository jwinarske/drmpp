
#include <algorithm>

#include "drmpp/vulkan/vulkan_kms.h"

#include "drmpp/logging/logging.h"

namespace drmpp::vulkan {

VulkanKms::VulkanKms(const bool enable_validation_layers,
                     const vk::ApplicationInfo& application_info,
                     const std::vector<const char*>& requested_extensions)
    : VulkanBase(VK_KHR_DISPLAY_EXTENSION_NAME,
                 enable_validation_layers,
                 application_info,
                 requested_extensions) {}

VulkanKms::~VulkanKms() = default;

vk::Result VulkanKms::InitializeVulkanKms(const std::string& device_path,
                                          const bool protected_chain) {
  // get the KMS device
  auto results = SelectKmsDevice(device_path);
  if (results.result != vk::Result::eSuccess) {
    return results.result;
  }
  const auto device = std::move(results.value);

  // get matching Vulkan physical device
  const auto phy_results = SelectPhysicalDevice(device, getVulkanInstance());
  if (phy_results.result != vk::Result::eSuccess) {
    return phy_results.result;
  }
  physical_device_ = phy_results.value;

  if (protected_chain) {
    vk::PhysicalDeviceFeatures2 features{};
    vk::PhysicalDeviceProtectedMemoryFeatures protected_features;
    features.pNext = &protected_features;
    physical_device_.getFeatures2(&features);

    if (!protected_features.protectedMemory) {
      LOG_WARN("Protected memory requested but not supported by device");
    }
    protected_ = protected_features.protectedMemory;
  }

  return vk::Result::eSuccess;
}

bool VulkanKms::HasExtensionProperty(
    const std::vector<vk::ExtensionProperties>& properties,
    const char* req) {
  const auto it = std::find_if(properties.begin(), properties.end(),
                               [req](const vk::ExtensionProperties& prop) {
                                 return std::strncmp(prop.extensionName, req,
                                                     std::strlen(req)) == 0;
                               });
  return it != properties.end();
}

bool VulkanKms::BussesMatch(drmDevicePtr drm_device,
                            vk::PhysicalDevice physical_device) {
  if (drm_device->bustype == DRM_BUS_PCI) {
    auto pci_bus_info = drm_device->businfo.pci;

    uint32_t count = 0;
    vk::Result result = physical_device.enumerateDeviceExtensionProperties(
        nullptr, &count, nullptr);
    if (result != vk::Result::eSuccess || count == 0) {
      LOG_ERROR("Could not enumerate device extensions properties");
      return false;
    }

    auto properties = std::vector<vk::ExtensionProperties>(count);
    result = physical_device.enumerateDeviceExtensionProperties(
        nullptr, &count, properties.data());
    if (result != vk::Result::eSuccess || count == 0) {
      return false;
    }

    if (!HasExtensionProperty(properties, VK_EXT_PCI_BUS_INFO_EXTENSION_NAME)) {
      LOG_ERROR("Physical device has no support for VK_EXT_pci_bus_info");
      return false;
    }

    vk::PhysicalDevicePCIBusInfoPropertiesEXT pci_props;
    vk::PhysicalDeviceProperties2 phy_dev_props;
    phy_dev_props.pNext = &pci_props;
    physical_device.getProperties2(&phy_dev_props);

    return pci_props.pciDomain == pci_bus_info->domain &&
           pci_props.pciBus == pci_bus_info->bus &&
           pci_props.pciDevice == pci_bus_info->dev &&
           pci_props.pciFunction == pci_bus_info->func;
  }

  return false;
}

bool VulkanKms::run() const {
  return true;
}

void VulkanKms::CheckVkResult(const VkResult err) {
  if (err == 0)
    return;
  LOG_ERROR("[VulkanKhr] Error: VkResult = {}", static_cast<int>(err));
  if (err < 0)
    abort();
}

}  // namespace drmpp::vulkan
