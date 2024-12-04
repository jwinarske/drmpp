
#include <cassert>
#include <memory>

#include <fcntl.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <sys/types.h>
#include <unistd.h>
#include <xf86drm.h>

#include "shared_libs/libdrm.h"
#include "shared_libs/libgbm.h"
#include "kms/device.h"
#include "logging/logging.h"

std::shared_ptr<KmsDevice> KmsDevice::AutoDetect() {
  std::shared_ptr<KmsDevice> device = nullptr;

  int num_devices = drm->GetDevices2(0, nullptr, 0);
  if (num_devices <= 0) {
    LOG_ERROR("no DRM devices available");
    return {};
  }

  std::vector<drmDevice*> devices(num_devices);
  num_devices = drm->GetDevices2(0, devices.data(), num_devices);
  for (const auto& candidate : devices) {
    // We need /dev/dri/cardN nodes for mode setting
    if (candidate->available_nodes & (1 << DRM_NODE_PRIMARY)) {
      continue;
    }
    device = Open(candidate->nodes[DRM_NODE_PRIMARY]);
    if (device) {
      break;
    }
  }

  drm->FreeDevices(devices.data(), static_cast<int>(devices.size()));

  if (!device) {
    LOG_ERROR("Could not find suitable KMS device");
  }
  return device;
}

std::shared_ptr<KmsDevice> KmsDevice::Open(const std::string& dev_node) {
  // open handle to drm device node
  int fd = open(dev_node.c_str(), O_RDWR | O_CLOEXEC, 0);
  if (fd < 0) {
    LOG_ERROR("Could not open {}: {}", dev_node.c_str(), strerror(errno));
    return nullptr;
  }

  // if not master exit
  drm_magic_t magic;
  if (drm->GetMagic(fd, &magic) != 0 || drm->AuthMagic(fd, magic) != 0) {
    LOG_ERROR("KMS device {} is not master", dev_node);
    close(fd);
    return nullptr;
  }

  // check for universal planes or atomic
  const int universal_planes =
      drm->SetClientCap(fd, DRM_CLIENT_CAP_UNIVERSAL_PLANES, 1);
  if (const int atomic_planes = drm->SetClientCap(fd, DRM_CLIENT_CAP_ATOMIC, 1);
      (universal_planes | atomic_planes) != 0) {
    LOG_ERROR("Device has no support for universal planes or atomic");
    close(fd);
    return nullptr;
  }

  uint64_t cap;
  const auto result = drm->GetCap(fd, DRM_CAP_ADDFB2_MODIFIERS, &cap);
  bool format_modifiers = (result == 0 && cap != 0);
  LOG_DEBUG("framebuffer modifiers {}",
            (format_modifiers) ? "are supported" : "are not supported");

  // Get card resources
  const auto resources = drm->ModeGetResources(fd);
  if (!resources) {
    LOG_ERROR("Could not get card resources for {}", dev_node);
    close(fd);
    return nullptr;
  }

  const auto plane_resources = drm->ModeGetPlaneResources(fd);
  if (!plane_resources) {
    LOG_ERROR("Device {} has no planes", dev_node);
    drm->ModeFreeResources(resources);
    close(fd);
    return nullptr;
  }

  if (resources->count_crtcs <= 0 || resources->count_connectors <= 0 ||
      resources->count_encoders <= 0 || plane_resources->count_planes <= 0) {
    LOG_INFO("Device {} is not a KMS device", dev_node);
    drm->ModeFreePlaneResources(plane_resources);
    drm->ModeFreeResources(resources);
    close(fd);
    return nullptr;
  }

  auto planes = std::vector<drmModePlanePtr>(plane_resources->count_planes);
  for (size_t i = 0; i < plane_resources->count_planes; i++) {
    planes[i] = drm->ModeGetPlane(fd, plane_resources->planes[i]);
    assert(planes[i]);
  }

  std::vector<std::unique_ptr<Output>> outputs{};
  for (auto i = 0; i < resources->count_connectors; i++) {
    const auto connector = drm->ModeGetConnector(fd, resources->connectors[i]);
    if (!connector) {
      LOG_ERROR("Could not get connector {}", i);
      continue;
    }
    auto output = Output::Create(fd, resources, planes, connector);
    if (!output)
      continue;
    outputs.push_back(std::move(output));
  }

  if (outputs.empty()) {
    LOG_ERROR("Device {} has no active outputs", dev_node);
    for (const auto& plane : planes) {
      drm->ModeFreePlane(plane);
    }
    planes.clear();
    drm->ModeFreePlaneResources(plane_resources);
    drm->ModeFreeResources(resources);
    close(fd);
    return nullptr;
  }

  // create GBM device for buffer allocation
  gbm_device* gbm_device = gbm->create_device(fd);

  LOG_DEBUG("using device {} with {} outputs", dev_node, outputs.size());

  return std::make_shared<KmsDevice>(fd, format_modifiers, gbm_device,
                                     resources, std::move(planes),
                                     std::move(outputs));
}

KmsDevice::KmsDevice(const int fd,
                     const bool format_modifiers,
                     gbm_device* gbm_device,
                     drmModeResPtr resources,
                     std::vector<drmModePlanePtr> planes,
                     std::vector<std::unique_ptr<Output>> outputs)
    : drm_fd_(fd),
      format_modifiers_(format_modifiers),
      gbm_device_(gbm_device),
      resources_(resources),
      planes_(std::move(planes)),
      outputs_(std::move(outputs)) {}

KmsDevice::~KmsDevice() {
  for (auto& output : outputs_) {
    output.reset();
  }
  for (const auto& plane : planes_) {
    drm->ModeFreePlane(plane);
  }
  if (resources_) {
    drm->ModeFreeResources(resources_);
  }
  if (gbm_device_) {
    gbm->device_destroy(gbm_device_);
  }
  close(drm_fd_);
}