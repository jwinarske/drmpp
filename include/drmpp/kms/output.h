#ifndef INCLUDE_DRMPP_KMS_OUTPUT_H
#define INCLUDE_DRMPP_KMS_OUTPUT_H

#include <memory>
#include <vector>

#include <xf86drmMode.h>

class Output {
 public:
  Output(uint32_t plane_id,
         uint32_t crtc_id,
         uint32_t connector_id,
         uint32_t connector_type,
         const drmModeModeInfo& mode,
         uint64_t refresh,
         bool needs_repaint,
         int commit_fence_fd);

  ~Output() = default;

  static std::unique_ptr<Output> Create(
      int drm_fd,
      const drmModeRes* resources,
      const std::vector<drmModePlanePtr>& planes,
      const drmModeConnector* connector);

 private:
  uint32_t plane_id_;
  uint32_t crtc_id_;
  uint32_t connector_id_;
  uint32_t connector_type_;
  drmModeModeInfo mode_;
  uint64_t refresh_;
  bool needs_repaint_;
  int commit_fence_fd_;
};

#endif  // INCLUDE_DRMPP_KMS_OUTPUT_H
