/*
 * Copyright (c) 2024 The drmpp Contributors
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <csignal>
#include <filesystem>
#include <fstream>
#include <iostream>

#include <cxxopts.hpp>

#include "drmpp/input/seat.h"
#include "drmpp/shared_libs/libdrm.h"
#include "drmpp/vulkan/vulkan_khr.h"

static struct Configuration {
  bool validate = false;
  vk::PresentModeKHR present_mode = vk::PresentModeKHR::eFifo;
  int gpu_number = -1;
  bool protected_chain = true;
  vk::SurfaceKHR surface = nullptr;
} gConfig;

static volatile bool gRunning = true;

class App final : public Logging,
                  public drmpp::vulkan::VulkanKhr,
                  public drmpp::input::KeyboardObserver,
                  public drmpp::input::PointerObserver,
                  public drmpp::input::SeatObserver {
 public:
  explicit App()
      : VulkanKhr(gConfig.validate,
                  {"vk-khr-inp", VK_MAKE_VERSION(0, 1, 0), "No Engine",
                   VK_MAKE_VERSION(1, 0, 0), VK_MAKE_VERSION(1, 1, 0), nullptr},
                  {}) {
    if (InitializeVulkanKHR(gConfig.gpu_number, gConfig.protected_chain) !=
        vk::Result::eSuccess) {
      LOG_ERROR("Unable to initialize Vulkan KMS");
      exit(EXIT_FAILURE);
    }
    seat_ = std::make_unique<drmpp::input::Seat>(false, "");
    seat_->register_observer(this, this);
    seat_->run_once();
  }

  ~App() override { seat_.reset(); }

  [[nodiscard]] bool run() const override { return seat_->run_once(); }

 private:
  std::unique_ptr<drmpp::input::Seat> seat_;
  std::mutex cmd_mutex_{};

  struct {
    uint32_t width;
    uint32_t height;
    uint32_t refresh_rate;
  } mode_{};

  vk::DisplayModeKHR display_mode_;

  static void PrintDisplayProperties(vk::DisplayPropertiesKHR const& props) {
    LOG_DEBUG("Display:");
    LOG_DEBUG("\tname: {}", props.displayName);
    LOG_DEBUG("\tphysical dimensions: {}x{}", props.physicalDimensions.width,
              props.physicalDimensions.height);
    LOG_DEBUG("\tphysical resolution: {}x{}", props.physicalResolution.width,
              props.physicalResolution.height);
    LOG_DEBUG("\tplane reorder: {}", props.planeReorderPossible ? "yes" : "no");
    LOG_DEBUG("\tpersistent content: {}",
              props.persistentContent ? "yes" : "no");
    LOG_DEBUG("\tsupported transforms: {}",
              static_cast<uint32_t>(props.supportedTransforms));
  }

  static void PrintDisplayModeProperties(
      vk::DisplayModePropertiesKHR const& props) {
    LOG_DEBUG("\tmode: {}x{} @ {}", props.parameters.visibleRegion.width,
              props.parameters.visibleRegion.height,
              props.parameters.refreshRate);
  }

  static void PrintDisplayPlaneProperties(
      vk::DisplayPlanePropertiesKHR const& props) {
    LOG_DEBUG("Plane:");
    LOG_DEBUG("\tcurrent stack index: {}", props.currentStackIndex);
  }

  static void PrintPhysicalDeviceProperties(
      vk::PhysicalDeviceProperties const& props) {
    LOG_DEBUG("Vendor ID 0x{:04X}, device name {}", props.vendorID,
              std::string_view(props.deviceName.data(),
                               std::strlen(props.deviceName.data())));
    LOG_DEBUG("Device ID 0x{:04X}", props.deviceID);
    LOG_DEBUG("Driver Version: {}.{}.{}", VK_VERSION_MAJOR(props.driverVersion),
              VK_VERSION_MINOR(props.driverVersion),
              VK_VERSION_PATCH(props.driverVersion));
    LOG_DEBUG("API Version: {}.{}.{}", VK_VERSION_MAJOR(props.apiVersion),
              VK_VERSION_MINOR(props.apiVersion),
              VK_VERSION_PATCH(props.apiVersion));
    std::ostringstream oss;
    oss << std::hex << std::setfill('0');
    for (size_t i = 0; i < props.pipelineCacheUUID.size(); ++i) {
      oss << std::setw(2) << static_cast<int>(props.pipelineCacheUUID[i]);
      if (i == 3 || i == 5 || i == 7 || i == 9) {
        oss << '-';
      }
    }
    LOG_DEBUG("Pipeline Cache UUID: {}", oss.str());
    switch (props.deviceType) {
      case vk::PhysicalDeviceType::eOther:
        LOG_DEBUG("Type: Other");
        break;
      case vk::PhysicalDeviceType::eIntegratedGpu:
        LOG_DEBUG("Type: Integrated GPU");
        break;
      case vk::PhysicalDeviceType::eDiscreteGpu:
        LOG_DEBUG("Type: Discrete GPU");
        break;
      case vk::PhysicalDeviceType::eVirtualGpu:
        LOG_DEBUG("Type: Virtual GPU");
        break;
      case vk::PhysicalDeviceType::eCpu:
        LOG_DEBUG("Type: CPU");
        break;
    }
  }

  vk::ResultValue<vk::PhysicalDevice> SelectPhysicalDevice(
      const int gpu_number,
      const vk::Instance& instance) override {
    auto gpus = instance.enumeratePhysicalDevices();
    CHECK_VK_RESULT(gpus.result);

    // command line selection
    if (gpu_number >= 0) {
      // invalid command line selection
      if (gpu_number > static_cast<int>(gpus.value.size())) {
        LOG_ERROR("GPU {} specified is not present, GPU count = {}",
                  gpus.value.size());
        exit(EXIT_FAILURE);
      }

      // display selection details
      PrintPhysicalDeviceProperties(gpus.value[gpu_number].getProperties());

      return vk::ResultValue<vk::PhysicalDevice>{vk::Result::eSuccess,
                                                 {gpus.value[gpu_number]}};
    }

    // auto select gpu
    if (gpu_number == -1) {
      for (auto& gpu : gpus.value) {
        auto properties = gpu.getProperties();
        PrintPhysicalDeviceProperties(properties);

        // check for queue families
        auto queue_family_props = gpu.getQueueFamilyProperties();
        if (queue_family_props.empty()) {
          LOG_WARN("Skipping {} due to lack of queue families.",
                   std::string_view(properties.deviceName.data(),
                                    std::strlen(properties.deviceName.data())));
          continue;
        }

        // check for graphics queue
        if ((queue_family_props[0].queueFlags & vk::QueueFlagBits::eGraphics) ==
            vk::QueueFlags{}) {
          LOG_WARN("Skipping {} due to lack of graphics queue.",
                   std::string_view(properties.deviceName.data(),
                                    std::strlen(properties.deviceName.data())));
          continue;
        }
#if 0
        // Continue if queue family does not support presentation to surface
        vk::Bool32 supported = VK_FALSE;
        if (const auto result =
                gpu.getSurfaceSupportKHR(0, getParentSurface(), &supported);
            result != vk::Result::eSuccess || !supported)
          continue;
#endif

        // select protected if available
        float queuePriorities[1] = {1.0f};
        const vk::DeviceQueueCreateInfo device_queue_create_info(
            vk::DeviceQueueCreateFlags(
                VulkanIsProtectedKHR() ? VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT
                                       : 0),
            0, 1, queuePriorities, nullptr);

        return vk::ResultValue{vk::Result::eSuccess, gpu};
      }
    }
    return {vk::Result::eErrorInitializationFailed, {}};
  }

  vk::ResultValue<std::pair<vk::DisplayKHR, vk::SurfaceKHR>>
  CreateDisplaySurface(const vk::Instance& instance,
                       const vk::PhysicalDevice& physical_device) override {
    auto displays = physical_device.getDisplayPropertiesKHR();
    CHECK_VK_RESULT(displays.result);
    if (displays.value.empty()) {
      return {vk::Result::eErrorInitializationFailed, {}};
    }

    vk::DisplayKHR selected_display{};
    struct {
      vk::DisplayPlanePropertiesKHR props;
      int index = 0;
      uint32_t stack_index = 0;
    } selected_plane;

    for (const auto& display : displays.value) {
      bool found_plane = false;

      PrintDisplayProperties(display);

      auto modes = physical_device.getDisplayModePropertiesKHR(display.display);
      CHECK_VK_RESULT(modes.result);
      if (modes.value.empty()) {
        LOG_ERROR("No modes available for display ({})", display.displayName);
        continue;
      }

      LOG_DEBUG("Modes for display ({})", display.displayName);
      for (const auto& mode : modes.value) {
        LOG_DEBUG("\t{}x{} @{:.3f}", mode.parameters.visibleRegion.width,
                  mode.parameters.visibleRegion.height,
                  static_cast<float>(mode.parameters.refreshRate) / 1000.0f);
      }

      // get list of planes
      const auto plane_props = physical_device.getDisplayPlanePropertiesKHR();
      CHECK_VK_RESULT(plane_props.result);
      if (plane_props.value.empty()) {
        LOG_ERROR("No planes found for display ({})", display.displayName);
        continue;
      }

      int index = 0;
      for (const auto& props : plane_props.value) {
        // skip planes bound to a different display
        if (props.currentDisplay != VK_NULL_HANDLE &&
            props.currentDisplay != display.display) {
          LOG_DEBUG("Skipping plane index {}, bound to different display",
                    index);
          index++;
          continue;
        }

        auto supported_displays =
            physical_device.getDisplayPlaneSupportedDisplaysKHR(
                props.currentStackIndex);
        CHECK_VK_RESULT(supported_displays.result);
        for (const auto& supported_display : supported_displays.value) {
          if (supported_display == display.display) {
            LOG_DEBUG("Found Plane: Index: {}, Stack Index: {}", index,
                      props.currentStackIndex);
            found_plane = VK_TRUE;
            selected_plane.index = index;
            selected_plane.stack_index = props.currentStackIndex;
            break;
          }
        }
        index++;
      }

      if (found_plane) {
        selected_display = display.display;
        break;
      }
    }

    if (selected_display == VK_NULL_HANDLE) {
      LOG_ERROR("Failed to find a display with a suitable plane");
      return {vk::Result::eErrorInitializationFailed, {}};
    }

    // Take the first mode - TODO match against desired mode
    const auto modes =
        physical_device.getDisplayModePropertiesKHR(selected_display);
    CHECK_VK_RESULT(modes.result);
    const vk::DisplayModePropertiesKHR mode_properties = modes.value[0];

    ///
    /// Find supported alpha mode
    ///
    const auto plane_caps = physical_device.getDisplayPlaneCapabilitiesKHR(
        mode_properties.displayMode, selected_plane.stack_index);
    CHECK_VK_RESULT(plane_caps.result);
    LOG_DEBUG(
        "   src pos: min: {},{} -> max: {},{}",
        plane_caps.value.minSrcPosition.x, plane_caps.value.minSrcPosition.y,
        plane_caps.value.maxSrcPosition.x, plane_caps.value.maxSrcPosition.y);
    LOG_DEBUG("   src size: min: {},{}-> max: {},{}",
              plane_caps.value.minSrcExtent.width,
              plane_caps.value.minSrcExtent.height,
              plane_caps.value.maxSrcExtent.width,
              plane_caps.value.maxSrcExtent.height);
    LOG_DEBUG(
        "   dst pos: min: {},{} -> max: {},{}",
        plane_caps.value.minDstPosition.x, plane_caps.value.minDstPosition.y,
        plane_caps.value.maxDstPosition.x, plane_caps.value.maxDstPosition.y);
    LOG_DEBUG("   dst size: min: {},{} -> max: {},{}",
              plane_caps.value.minDstExtent.width,
              plane_caps.value.minDstExtent.height,
              plane_caps.value.maxDstExtent.width,
              plane_caps.value.maxDstExtent.height);

    // Find a supported alpha mode
    constexpr vk::DisplayPlaneAlphaFlagBitsKHR alpha_modes[4] = {
        vk::DisplayPlaneAlphaFlagBitsKHR::eOpaque,
        vk::DisplayPlaneAlphaFlagBitsKHR::eGlobal,
        vk::DisplayPlaneAlphaFlagBitsKHR::ePerPixel,
        vk::DisplayPlaneAlphaFlagBitsKHR::ePerPixelPremultiplied,
    };
    auto alpha_mode(vk::DisplayPlaneAlphaFlagBitsKHR::eOpaque);
    for (uint32_t i = 0; i < sizeof(alpha_modes); i++) {
      if (plane_caps.value.supportedAlpha & alpha_modes[i]) {
        alpha_mode = alpha_modes[i];
        break;
      }
    }
    vk::Extent2D image_extent(mode_properties.parameters.visibleRegion.width,
                              mode_properties.parameters.visibleRegion.height);

    vk::DisplaySurfaceCreateInfoKHR create_info(
        vk::DisplaySurfaceCreateFlagsKHR(), mode_properties.displayMode,
        selected_plane.index, selected_plane.props.currentStackIndex,
        vk::SurfaceTransformFlagBitsKHR::eIdentity, 1.0f, alpha_mode,
        image_extent);

    auto surface = instance.createDisplayPlaneSurfaceKHR(create_info);
    CHECK_VK_RESULT(surface.result);
    return vk::ResultValue<std::pair<vk::DisplayKHR, vk::SurfaceKHR>>{
        vk::Result::eSuccess, std::make_pair(selected_display, surface.value)};
  }

  vk::Result InitializePhysicalDeviceKHR(
      const vk::Instance& instance,
      const vk::PhysicalDevice& physical_device) override {
    ///
    /// Display Properties
    ///
    vk::DisplayPropertiesKHR display;
    vk::DisplayModePropertiesKHR mode_properties;

    auto displays = physical_device.getDisplayPropertiesKHR();
    CHECK_VK_RESULT(displays.result);
    if (displays.value.empty()) {
      return vk::Result::eNotReady;
    }
#if !defined(NDEBUG)
    for (const auto& disp : displays.value) {
      PrintDisplayProperties(disp);
    }
#endif
    display = displays.value[0];

    const auto modes =
        physical_device.getDisplayModeProperties2KHR(display.display);
    CHECK_VK_RESULT(modes.result);
    if (!modes.value.empty()) {
#if !defined(NDEBUG)
      for (const auto& p : modes.value) {
        PrintDisplayModeProperties(p.displayModeProperties);
      }
#endif
      mode_.width =
          modes.value[0].displayModeProperties.parameters.visibleRegion.width;
      mode_.height =
          modes.value[0].displayModeProperties.parameters.visibleRegion.height;
      mode_.refresh_rate =
          modes.value[0].displayModeProperties.parameters.refreshRate;

      LOG_DEBUG("Mode (0) {}x{} @{}", mode_.width, mode_.height,
                mode_.refresh_rate);

      mode_properties = modes.value[0].displayModeProperties;
    } else {
      LOG_ERROR("No modes available for display ({})", display.displayName);
    }

    ///
    /// Plane Properties
    ///
    const auto planes = physical_device.getDisplayPlanePropertiesKHR();
    CHECK_VK_RESULT(planes.result);
    if (planes.value.empty()) {
      LOG_ERROR("No planes available for display ({})", display.displayName);
    } else {
      LOG_DEBUG("Plane Properties:");
      for (const auto& plane : planes.value) {
        LOG_DEBUG("\tcurrent stack index: {}", plane.currentStackIndex);

        auto supported_displays =
            physical_device.getDisplayPlaneSupportedDisplaysKHR(
                plane.currentStackIndex);
        CHECK_VK_RESULT(supported_displays.result);

        for (const auto& supported_display : supported_displays.value) {
          if (supported_display == plane.currentDisplay) {
            LOG_DEBUG(" (current)");
          }
        }
        auto plane_caps = physical_device.getDisplayPlaneCapabilitiesKHR(
            mode_properties.displayMode, plane.currentStackIndex);
        CHECK_VK_RESULT(plane_caps.result);
        LOG_DEBUG("   src pos: min: {},{} -> max: {},{}",
                  plane_caps.value.minSrcPosition.x,
                  plane_caps.value.minSrcPosition.y,
                  plane_caps.value.maxSrcPosition.x,
                  plane_caps.value.maxSrcPosition.y);
        LOG_DEBUG("   src size: min: {},{}-> max: {},{}",
                  plane_caps.value.minSrcExtent.width,
                  plane_caps.value.minSrcExtent.height,
                  plane_caps.value.maxSrcExtent.width,
                  plane_caps.value.maxSrcExtent.height);
        LOG_DEBUG("   dst pos: min: {},{} -> max: {},{}",
                  plane_caps.value.minDstPosition.x,
                  plane_caps.value.minDstPosition.y,
                  plane_caps.value.maxDstPosition.x,
                  plane_caps.value.maxDstPosition.y);
        LOG_DEBUG("   dst size: min: {},{} -> max: {},{}",
                  plane_caps.value.minDstExtent.width,
                  plane_caps.value.minDstExtent.height,
                  plane_caps.value.maxDstExtent.width,
                  plane_caps.value.maxDstExtent.height);
      }
    }

    const vk::DisplayModeCreateInfoKHR display_mode_create_info(
        vk::DisplayModeCreateFlagsKHR(), mode_properties.parameters, nullptr);
    CHECK_VK_RESULT(physical_device.createDisplayModeKHR(
        display.display, &display_mode_create_info, nullptr, &display_mode_));

    const vk::DisplaySurfaceCreateInfoKHR create_info(
        vk::DisplaySurfaceCreateFlagsKHR(), display_mode_, 0, 0,
        vk::SurfaceTransformFlagBitsKHR::eIdentity, 0.0f,
        vk::DisplayPlaneAlphaFlagBitsKHR::eOpaque,
        mode_properties.parameters.visibleRegion);

    auto surface = instance.createDisplayPlaneSurfaceKHR(create_info);
    CHECK_VK_RESULT(surface.result);
    LOG_DEBUG("Created KHR Display Plane Surface");

#if 0
    // init_vk_objects(vc);
    // create_swapchain(vc);
#endif
    return vk::Result::eSuccess;
  }

  vk::ResultValue<std::pair<vk::Device, vk::PhysicalDevice>>
  InitializeDeviceKHR(const vk::Instance& instance) override {
    auto gpus = instance.enumeratePhysicalDevices();
    CHECK_VK_RESULT(gpus.result);

    for (const auto& gpu : gpus.value) {
      auto properties = gpu.getProperties2();
      PrintPhysicalDeviceProperties(properties.properties);

      auto queue_family_props = gpu.getQueueFamilyProperties();
      if (queue_family_props.empty()) {
        LOG_WARN("Skipping {} due to lack of queue families.",
                 std::string_view(
                     properties.properties.deviceName.data(),
                     std::strlen(properties.properties.deviceName.data())));
        continue;
      }

      if ((queue_family_props[0].queueFlags & vk::QueueFlagBits::eGraphics) ==
          vk::QueueFlags{}) {
        LOG_WARN("Skipping {} due to lack of graphics queue.",
                 std::string_view(
                     properties.properties.deviceName.data(),
                     std::strlen(properties.properties.deviceName.data())));
        break;
      }
      float queuePriorities[1] = {1.0f};
      const vk::DeviceQueueCreateInfo device_queue_create_info(
          vk::DeviceQueueCreateFlags(VulkanIsProtectedKHR()
                                         ? VK_DEVICE_QUEUE_CREATE_PROTECTED_BIT
                                         : 0),
          0, 1, queuePriorities, nullptr);

      const vk::DeviceCreateInfo device_create_info(
          vk::DeviceCreateFlags(0), 1, &device_queue_create_info, 0, nullptr, 1,
          (const char* const[]){
              VK_KHR_SWAPCHAIN_EXTENSION_NAME,
          },
          nullptr, nullptr);

      vk::Device device;
      CHECK_VK_RESULT(gpu.createDevice(&device_create_info, nullptr, &device));

      return vk::ResultValue<std::pair<vk::Device, vk::PhysicalDevice>>{
          vk::Result::eSuccess, {device, gpu}};
    }
    return {vk::Result::eErrorInitializationFailed, {}};
  }

  void notify_seat_capabilities(drmpp::input::Seat* seat,
                                uint32_t caps) override {
    LOG_INFO("Seat Capabilities: {}", caps);
    if (caps & SEAT_CAPABILITIES_POINTER) {
      if (const auto pointer = seat_->get_pointer(); pointer.has_value()) {
        pointer.value()->register_observer(this, this);
      }
    }
    if (caps & SEAT_CAPABILITIES_KEYBOARD) {
      if (const auto keyboards = seat_->get_keyboards();
          keyboards.has_value()) {
        for (auto const& keyboard : *keyboards.value()) {
          keyboard->register_observer(this, this);
        }
      }
    }
  }

  void notify_keyboard_xkb_v1_key(
      drmpp::input::Keyboard* keyboard,
      uint32_t time,
      uint32_t xkb_scancode,
      bool keymap_key_repeats,
      const uint32_t state,
      int xdg_key_symbol_count,
      const xkb_keysym_t* xdg_key_symbols) override {
    if (state == LIBINPUT_KEY_STATE_PRESSED) {
      if (xdg_key_symbols[0] == XKB_KEY_Escape ||
          xdg_key_symbols[0] == XKB_KEY_q || xdg_key_symbols[0] == XKB_KEY_Q) {
        std::scoped_lock lock(cmd_mutex_);
        exit(EXIT_SUCCESS);
      }
      if (xdg_key_symbols[0] == XKB_KEY_d) {
        std::scoped_lock lock(cmd_mutex_);
      } else if (xdg_key_symbols[0] == XKB_KEY_b) {
        std::scoped_lock lock(cmd_mutex_);
      }
    }
    LOG_INFO(
        "Key: time: {}, xkb_scancode: 0x{:X}, key_repeats: {}, state: {}, "
        "xdg_keysym_count: {}, syms_out[0]: 0x{:X}",
        time, xkb_scancode, keymap_key_repeats,
        state == LIBINPUT_KEY_STATE_PRESSED ? "press" : "release",
        xdg_key_symbol_count, xdg_key_symbols[0]);
  }

  void notify_pointer_motion(drmpp::input::Pointer* pointer,
                             uint32_t time,
                             double sx,
                             double sy) override {
    LOG_TRACE("x: {}, y: {}", sx, sy);
  }

  void notify_pointer_button(drmpp::input::Pointer* pointer,
                             uint32_t serial,
                             uint32_t time,
                             uint32_t button,
                             uint32_t state) override {
    LOG_INFO("button: {}, state: {}", button, state);
  }

  void notify_pointer_axis(drmpp::input::Pointer* pointer,
                           uint32_t time,
                           uint32_t axis,
                           double value) override {
    LOG_INFO("axis: {}", axis);
  }

  void notify_pointer_frame(drmpp::input::Pointer* pointer) override {
    LOG_INFO("frame");
  }

  void notify_pointer_axis_source(drmpp::input::Pointer* pointer,
                                  uint32_t axis_source) override {
    LOG_INFO("axis_source: {}", axis_source);
  }

  void notify_pointer_axis_stop(drmpp::input::Pointer* pointer,
                                uint32_t time,
                                uint32_t axis) override {
    LOG_INFO("axis_stop: {}", axis);
  }

  void notify_pointer_axis_discrete(drmpp::input::Pointer* pointer,
                                    uint32_t axis,
                                    int32_t discrete) override {
    LOG_INFO("axis_discrete: axis: {}, discrete: {}", axis, discrete);
  }
};

int main(const int argc, char** argv) {
  std::signal(SIGINT, [](const int signal) {
    if (signal == SIGINT) {
      gRunning = false;
    }
  });

  int present_mode = static_cast<int>(gConfig.present_mode);
  cxxopts::Options options("vk-khr-inp", "Vulkan KHR input example");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()
      // clang-format off
  ("help", "Print help")
  ("v,validate", "Enable Vulkan Validation", cxxopts::value<bool>(gConfig.validate))
  ("p,present-mode", "Vulkan Present Mode", cxxopts::value<int>(present_mode))
  ("g,gpu-number", "Vulkan GPU Number", cxxopts::value<int>(gConfig.gpu_number));
  // clang-format on

  gConfig.present_mode = static_cast<vk::PresentModeKHR>(present_mode);

  if (options.parse(argc, argv).count("help")) {
    spdlog::info("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  const App app;

  while (gRunning && app.run()) {
  }

  return EXIT_SUCCESS;
}
