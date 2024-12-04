#include "drmpp/vulkan/vulkan_base.h"

#include "drmpp/logging/logging.h"

VULKAN_HPP_DEFAULT_DISPATCH_LOADER_DYNAMIC_STORAGE

const auto& d = vk::detail::defaultDispatchLoaderDynamic;

namespace drmpp::vulkan {

VulkanBase::VulkanBase(const char* required_extension,
                       const bool enable_validation_layers,
                       const vk::ApplicationInfo& application_info,
                       const std::vector<const char*>& requested_extensions) {
  VULKAN_HPP_DEFAULT_DISPATCHER.init();

  ///
  /// Extensions
  ///

  bool surface_extension_present = false;
  bool platform_extension_present = false;
  auto extensions = vk::enumerateInstanceExtensionProperties();
  CHECK_VK_RESULT(extensions.result);
  DLOG_DEBUG("Available Instance Extensions");
  for (const auto& l : extensions.value) {
    DLOG_DEBUG(
        "\t{} {}",
        std::string_view(l.extensionName.data(), std::strlen(l.extensionName)),
        std::to_string(l.specVersion));

    if (strcmp(l.extensionName, required_extension) == 0) {
      instance_.enabled_extensions.push_back(strdup(l.extensionName));
      platform_extension_present = true;
      continue;
    }

    if (strcmp(l.extensionName, VK_KHR_SURFACE_EXTENSION_NAME) == 0) {
      instance_.enabled_extensions.push_back(strdup(l.extensionName));
      surface_extension_present = true;
      continue;
    }

    if (enable_validation_layers) {
      if (strcmp(l.extensionName, VK_EXT_VALIDATION_FEATURES_EXTENSION_NAME) ==
              0 ||
          strcmp(l.extensionName, VK_EXT_DEBUG_UTILS_EXTENSION_NAME) == 0 ||
          strcmp(l.extensionName, VK_EXT_DEBUG_REPORT_EXTENSION_NAME) == 0) {
        instance_.enabled_extensions.push_back(strdup(l.extensionName));
        continue;
      }
    }

    if (!requested_extensions.empty()) {
      for (const auto& requested : requested_extensions) {
        if (strcmp(l.extensionName, requested) == 0) {
          instance_.enabled_extensions.push_back(strdup(l.extensionName));
          break;
        }
      }
    }
  }

  if (!instance_.enabled_extensions.empty()) {
    LOG_DEBUG("Enabled Instance Extensions");
    for (const auto& ext : instance_.enabled_extensions) {
      LOG_DEBUG("\t{}", ext);
    }
  }

  if (!(surface_extension_present && platform_extension_present)) {
    LOG_CRITICAL("Required Instance Extensions missing: {}\n{}",
                 VK_KHR_SURFACE_EXTENSION_NAME, required_extension);
    exit(EXIT_FAILURE);
  }

  VkLayerSettingsCreateInfoEXT create_info_ext = {
      VK_STRUCTURE_TYPE_LAYER_SETTINGS_CREATE_INFO_EXT, nullptr, 0, nullptr};

  vk::InstanceCreateInfo instance_create_info(
      {}, &application_info, 0, nullptr,
      static_cast<uint32_t>(instance_.enabled_extensions.size()),
      instance_.enabled_extensions.data(), &create_info_ext);

  ///
  /// Layers
  ///
  constexpr char VK_LAYER_KHRONOS_VALIDATION_NAME[] =
      "VK_LAYER_KHRONOS_validation";

  auto available_layers = vk::enumerateInstanceLayerProperties();
  CHECK_VK_RESULT(available_layers.result);
  if (!available_layers.value.empty()) {
    DLOG_DEBUG("Available Instance Layers");
    for (const auto& l : available_layers.value) {
      DLOG_DEBUG(
          "\t{} - {}",
          std::string_view(l.layerName.data(), std::strlen(l.layerName)),
          std::string_view(l.description.data(), std::strlen(l.description)));
      if (enable_validation_layers &&
          strcmp(l.layerName, VK_LAYER_KHRONOS_VALIDATION_NAME) == 0) {
        instance_.enabled_layers.push_back(VK_LAYER_KHRONOS_VALIDATION_NAME);

        constexpr char layer_name[] = "VK_LAYER_KHRONOS_validation";
        constexpr VkBool32 setting_validate_core = VK_TRUE;
        constexpr VkBool32 setting_validate_sync = VK_TRUE;
        constexpr VkBool32 setting_thread_safety = VK_TRUE;
        const char* setting_debug_action[] = {"VK_DBG_LAYER_ACTION_LOG_MSG"};
        const char* setting_report_flags[] = {"info", "warn", "perf", "error",
                                              "debug"};
        constexpr VkBool32 setting_enable_message_limit = VK_TRUE;
        constexpr int32_t setting_duplicate_message_limit = 3;

        const VkLayerSettingEXT settings[] = {
            {layer_name, "validate_core", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &setting_validate_core},
            {layer_name, "validate_sync", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &setting_validate_sync},
            {layer_name, "thread_safety", VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &setting_thread_safety},
            {layer_name, "debug_action", VK_LAYER_SETTING_TYPE_STRING_EXT, 1,
             setting_debug_action},
            {layer_name, "report_flags", VK_LAYER_SETTING_TYPE_STRING_EXT,
             static_cast<uint32_t>(std::size(setting_report_flags)),
             setting_report_flags},
            {layer_name, "enable_message_limit",
             VK_LAYER_SETTING_TYPE_BOOL32_EXT, 1,
             &setting_enable_message_limit},
            {layer_name, "duplicate_message_limit",
             VK_LAYER_SETTING_TYPE_INT32_EXT, 1,
             &setting_duplicate_message_limit}};

        create_info_ext.settingCount = std::size(settings);
        create_info_ext.pSettings = settings;

        LOG_DEBUG("{} Settings", layer_name);
        for (const auto& it : settings) {
          LOG_DEBUG("\t{}", it.pSettingName);
        }
        break;
      }
    }

    if (!instance_.enabled_layers.empty()) {
      LOG_DEBUG("Enabled Layer Extensions");
      for (const auto& ext : instance_.enabled_layers) {
        LOG_DEBUG("\t{}", ext);
      }
    }
  }

  instance_create_info.enabledLayerCount = instance_.enabled_layers.size();
  instance_create_info.ppEnabledLayerNames = instance_.enabled_layers.data();

  const auto result =
      createInstance(&instance_create_info, nullptr, &instance_.instance);
  if (result == vk::Result::eErrorIncompatibleDriver) {
    LOG_CRITICAL(
        "Cannot find a compatible Vulkan installable client driver (ICD)");
    exit(EXIT_FAILURE);
  }
  if (result == vk::Result::eErrorExtensionNotPresent) {
    LOG_CRITICAL("Cannot find a specified extension library.");
    exit(EXIT_FAILURE);
  }
  if (result != vk::Result::eSuccess) {
    LOG_CRITICAL("vkCreateInstance failed.");
    exit(EXIT_FAILURE);
  }

  VULKAN_HPP_DEFAULT_DISPATCHER.init(instance_.instance);
}

}  // namespace drmpp::vulkan
