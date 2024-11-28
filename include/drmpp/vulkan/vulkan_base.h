#ifndef EXAMPLES_VULKAN_VULKAN_H_
#define EXAMPLES_VULKAN_VULKAN_H_

#define VULKAN_HPP_NO_EXCEPTIONS 1
#define VULKAN_HPP_NO_STRUCT_SETTERS 1
#define VULKAN_HPP_DISPATCH_LOADER_DYNAMIC 1
#include <vulkan/vulkan.hpp>

#define S1(x) #x
#define S2(x) S1(x)
#define LOCATION __FILE__ " : " S2(__LINE__)

#define CHECK_VK_RESULT(x)                                         \
  do {                                                             \
    vk::detail::resultCheck(static_cast<vk::Result>(x), LOCATION); \
  } while (0)

namespace drmpp::vulkan {

class VulkanBase {
 public:
  explicit VulkanBase(
      const char* required_extension,
      bool enable_validation_layers,
      const vk::ApplicationInfo& application_info,
      const std::vector<const char*>& requested_extensions = {});

  virtual ~VulkanBase() = default;

  [[nodiscard]] vk::Instance getVulkanInstance() const {
    return instance_.instance;
  }

  bool CheckExtensionEnabled(const char* extension) {
    return std::any_of(instance_.enabled_extensions.begin(),
                       instance_.enabled_extensions.end(),
                       [extension](const char* enabled_extension) {
                         return strcmp(enabled_extension, extension) == 0;
                       });
  }

 private:
  struct {
    std::vector<const char*> enabled_extensions;
    std::vector<const char*> enabled_layers;
    vk::Instance instance;
  } instance_;

  vk::Device device_;
  vk::SurfaceKHR surface_{};

  bool debugUtilsSupported_{};
  bool enable_validation_layers_{};
};
}  // namespace drmpp::vulkan

#endif  // EXAMPLES_VULKAN_VULKAN_H_
