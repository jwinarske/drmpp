
#include "kms/output.h"

#include <cassert>

#include "kms/device.h"
#include "logging/logging.h"

Output::Output(const uint32_t plane_id,
               const uint32_t crtc_id,
               const uint32_t connector_id,
               const uint32_t connector_type,
               const drmModeModeInfo& mode,
               const uint64_t refresh,
               const bool needs_repaint,
               const int commit_fence_fd)
    : plane_id_(plane_id),
      crtc_id_(crtc_id),
      connector_id_(connector_id),
      connector_type_(connector_type),
      mode_(mode),
      refresh_(refresh),
      needs_repaint_(needs_repaint),
      commit_fence_fd_(commit_fence_fd) {}

std::unique_ptr<Output> Output::Create(
    const int drm_fd,
    const drmModeRes* resources,
    const std::vector<drmModePlanePtr>& planes,
    const drmModeConnector* connector) {
  std::unique_ptr<Output> output = nullptr;

  // if no encoder exit
  if (connector->encoder_id == 0) {
    return nullptr;
  }

  // find encoder
  drmModeEncoderPtr encoder = nullptr;
  for (int e = 0; e < resources->count_encoders; e++) {
    if (resources->encoders[e] == connector->encoder_id) {
      encoder = drmModeGetEncoder(drm_fd, resources->encoders[e]);
      break;
    }
  }
  assert(encoder);

  // if no crtc exit
  if (encoder->crtc_id == 0) {
    LOG_DEBUG("connector_id {}: no crtc", connector->connector_id);
    return nullptr;
  }

  // find crtc
  drmModeCrtcPtr crtc = nullptr;
  for (int c = 0; c < resources->count_crtcs; c++) {
    if (resources->crtcs[c] == encoder->crtc_id) {
      crtc = drmModeGetCrtc(drm_fd, resources->crtcs[c]);
      break;
    }
  }
  assert(crtc);

  // if no buffer exit
  if (crtc->buffer_id == 0) {
    LOG_DEBUG("connector_id {}: not active", connector->connector_id);
    return nullptr;
  }

  // find plane
  drmModePlanePtr plane = nullptr;
  for (auto p : planes) {
    if (p->crtc_id == crtc->crtc_id && p->fb_id == crtc->buffer_id) {
      LOG_DEBUG("plane_id {}, crtc_id {}, fb_id {}", p->plane_id, p->crtc_id,
                p->fb_id);
      plane = p;
      break;
    }
  }
  assert(plane);

  const uint64_t refresh = ((crtc->mode.clock * 1000000LL / crtc->mode.htotal) +
                            (crtc->mode.vtotal / 2)) /
                           crtc->mode.vtotal;

  LOG_DEBUG("crtc_id {}, connector_id {}, plane_id {}, active at {}x{}, {} mHz",
            crtc->crtc_id, connector->connector_id, plane->plane_id,
            crtc->width, crtc->height, refresh);

  output = std::make_unique<Output>(
      plane->plane_id, crtc->crtc_id, connector->connector_id,
      connector->connector_type, crtc->mode, refresh, true, -1);

  drmModeFreeCrtc(crtc);
  drmModeFreeEncoder(encoder);

  return output;
}
