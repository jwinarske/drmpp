#ifndef INCLUDE_DRMPP_KMS_DEVICE_H
#define INCLUDE_DRMPP_KMS_DEVICE_H

#include <memory>
#include <string>
#include <vector>

#include <gbm.h>
#include <xf86drmMode.h>

#include "kms/output.h"

class KmsDevice {
 public:
  KmsDevice(int fd,
            bool format_modifiers,
            gbm_device* gbm_device,
            drmModeResPtr resources,
            std::vector<drmModePlanePtr> planes,
            std::vector<std::unique_ptr<Output>> outputs);
  ~KmsDevice();

  static std::shared_ptr<KmsDevice> Open(const std::string& dev_node);

  static std::shared_ptr<KmsDevice> AutoDetect();

  [[nodiscard]] bool HasFormatModifiers() const { return format_modifiers_; }

  [[nodiscard]] int GetDrmFd() const { return drm_fd_; }

 private:
  int drm_fd_;
  bool format_modifiers_;
  gbm_device* gbm_device_;
  drmModeResPtr resources_;
  std::vector<drmModePlanePtr> planes_;
  std::vector<std::unique_ptr<Output>> outputs_;
};

#endif  // INCLUDE_DRMPP_KMS_DEVICE_H