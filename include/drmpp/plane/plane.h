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

#ifndef INCLUDE_DRMPP_PLANE_PLANE_H_
#define INCLUDE_DRMPP_PLANE_PLANE_H_

#include "drmpp.h"

namespace drmpp::plane {

/**
 * \brief Class containing common functions and structures for plane management.
 */
class Common {
 public:
  /**
   * \brief Structure representing a dumb framebuffer.
   */
  struct dumb_fb {
    uint32_t format; /**< Pixel format of the framebuffer */
    uint32_t width;  /**< Width of the framebuffer */
    uint32_t height; /**< Height of the framebuffer */
    uint32_t stride; /**< Stride of the framebuffer */
    uint32_t size;   /**< Size of the framebuffer */
    uint32_t handle; /**< Handle of the framebuffer */
    uint32_t id;     /**< ID of the framebuffer */
  };

  /**
   * \brief Picks a connector from the available DRM resources.
   *
   * \param drm_fd File descriptor for the DRM device.
   * \param drm_res Pointer to the DRM resources.
   * \return Pointer to the selected DRM connector.
   */
  static drmModeConnector* pick_connector(int drm_fd,
                                          const drmModeRes* drm_res);

  /**
   * \brief Picks a CRTC for the given connector from the available DRM
   * resources.
   *
   * \param drm_fd File descriptor for the DRM device.
   * \param drm_res Pointer to the DRM resources.
   * \param connector Pointer to the DRM connector.
   * \return Pointer to the selected DRM CRTC.
   */
  static drmModeCrtc* pick_crtc(int drm_fd,
                                const drmModeRes* drm_res,
                                const drmModeConnector* connector);

  /**
   * \brief Disables all CRTCs except the specified one.
   *
   * \param drm_fd File descriptor for the DRM device.
   * \param drm_res Pointer to the DRM resources.
   * \param crtc_id ID of the CRTC to keep enabled.
   */
  static void disable_all_crtcs_except(int drm_fd,
                                       const drmModeRes* drm_res,
                                       uint32_t crtc_id);

  /**
   * \brief Initializes a dumb framebuffer.
   *
   * \param fb Pointer to the dumb framebuffer structure.
   * \param drm_fd File descriptor for the DRM device.
   * \param format Pixel format of the framebuffer.
   * \param width Width of the framebuffer.
   * \param height Height of the framebuffer.
   * \return True if initialization was successful, false otherwise.
   */
  static bool dumb_fb_init(dumb_fb* fb,
                           int drm_fd,
                           uint32_t format,
                           uint32_t width,
                           uint32_t height);

  /**
   * \brief Maps a dumb framebuffer to memory.
   *
   * \param fb Pointer to the dumb framebuffer structure.
   * \param drm_fd File descriptor for the DRM device.
   * \return Pointer to the mapped memory.
   */
  static void* dumb_fb_map(dumb_fb const* fb, int drm_fd);

  /**
   * \brief Fills a dumb framebuffer with a specified color.
   *
   * \param fb Pointer to the dumb framebuffer structure.
   * \param drm_fd File descriptor for the DRM device.
   * \param color Color to fill the framebuffer with.
   */
  static void dumb_fb_fill(dumb_fb const* fb, int drm_fd, uint32_t color);
};

}  // namespace drmpp::plane

#endif  // INCLUDE_DRMPP_PLANE_PLANE_H_