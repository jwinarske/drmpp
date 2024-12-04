/*
 * Copyright 2024 drmpp contributors
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

#ifndef INCLUDE_DRMPP_OUTPUT_H_
#define INCLUDE_DRMPP_OUTPUT_H_

#include <unordered_map>

extern "C" {
#include <libliftoff.h>
}

#include "composition.h"
#include "device.h"

namespace drmpp {

/**
 * @brief An output akin to a logical display, it's something (a physical
 * display, writeback connector, multiple physical displays in clone mode
 * (currently unimplemented)) that can show a Composition.
 *
 * Usually, you create an Output with a CRTC id and a connector id,
 * in which case it will present composition on the display connected
 * to the connector you specified.
 */
class Output {
 public:
  /**
   * @brief Create an Output object for the given CRTC and connector, and mode.
   *
   */
  explicit Output(Device& device,
                  uint32_t crtc_id,
                  uint32_t connector_id,
                  const drmModeModeInfo& mode)
      : device_(device),
        crtc_id_(crtc_id),
        connector_id_(connector_id),
        mode_(mode),
        output_(::liftoff_output_create(device.liftoff_device(), crtc_id),
                ::liftoff_output_destroy) {}

  virtual ~Output() {
    if (mode_blob_id_ != 0) {
      drmModeDestroyPropertyBlob(device_.fd(), mode_blob_id_);
    }
  }

  // Disallow copy, copy-assign, move, move-assign
  Output(const Output&) = delete;
  Output& operator=(const Output&) = delete;
  Output(Output&&) = delete;
  Output& operator=(Output&&) = delete;

  /**
   * @brief Present the given composition on the output and
   *        wait for it to be displayed.
   */
  virtual bool present(const Composition& composition);

  /**
   * @brief Get the CRTC ID of the output.
   */
  uint32_t crtc_id() const { return crtc_id_; }

  /**
   * @brief Get the connector ID of the output.
   */
  uint32_t connector_id() const { return connector_id_; }

  /**
   * @brief The current video mode of the output.
   */
  const drmModeModeInfo& mode() const { return mode_; }

  /**
   * @brief The current (precise) refresh rate of the output.
   */
  double refreshRate() const {
    return mode_.clock * 1000.0 / (mode_.htotal * mode_.vtotal);
  }

  /**
   * @brief The current width (in pixels) of the output, based on the current
   * video mode.
   */
  uint32_t width() const { return mode_.hdisplay; }

  /**
   * @brief The current height (in pixels) of the output, based on the current
   * video mode.
   */
  uint32_t height() const { return mode_.vdisplay; }

 private:
  /// The ids of the "MODE" and "ACTIVE" properties of the CRTC.
  struct Props {
    uint32_t mode_id = 0;
    uint32_t active = 0;
  };

  Device& device_;
  const uint32_t crtc_id_;
  const uint32_t connector_id_;
  const drmModeModeInfo mode_;

  // The liftoff_output object for this output.
  std::unique_ptr<struct liftoff_output, decltype(&::liftoff_output_destroy)>
      output_;

  // True if we already applied our desired mode on the CRTC.
  bool did_set_mode = false;

  // The id of the property blob that contains the uploaded video mode.
  uint32_t mode_blob_id_ = 0;

  /**
   * @brief A vector containing all the liftoff_layers that were created
   * for this output.
   *
   * Every created layer is added to this vector so we can reuse them
   * for the next frame.
   */
  std::vector<
      std::unique_ptr<struct liftoff_layer, decltype(&::liftoff_layer_destroy)>>
      layers_;

  /// TODO: Invalidate the cache sometimes.
  std::unordered_map<Buffer*, std::unique_ptr<Framebuffer>> fb_cache_;

  std::optional<Props> crtc_props_;

  /**
   * @brief Add the given buffer as a framebuffer to the KMS device,
   *        and add it to an internal cache.
   *
   * If the buffer was already added before, the framebuffer ID is returned.
   *
   * @returns The framebuffer ID, suitable as an argument to
   * liftoff_layer_set_property(..., "FB_ID", ...) of the added buffer, or 0
   * if the buffer could not be added.
   */
  uint32_t addFramebuffer(Buffer* buffer);

  /**
   * @brief Resolve some basic property ids, such as the MODE_ID and ACTIVE
   * CRTC properties.
   */
  static std::optional<Props> resolveProps(int fd, uint32_t crtc_id);

  /**
   * @brief Apply the desired video mode (stored in member mode_)
   * on the atomic request.
   */
  bool applyMode(drmModeAtomicReq* req);
};

}  // namespace drmpp

#endif  // INCLUDE_DRMPP_OUTPUT_H_
