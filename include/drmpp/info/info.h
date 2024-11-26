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

#ifndef INCLUDE_DRMPP_KMS_INFO_INFO_H_
#define INCLUDE_DRMPP_KMS_INFO_INFO_H_

#include <vector>

#include <rapidjson/document.h>
#include <xf86drmMode.h>

namespace drmpp::info {
/**
 * \brief Struct containing methods to retrieve DRM information.
 */
struct DrmInfo {
  /**
   * \brief Retrieves node information for the given path.
   *
   * \param path The path to the DRM node.
   * \return A string containing the node information.
   */
  static std::string get_node_info(const std::string& path);

  /**
   * \brief Retrieves node information for the vector of device nodes.
   *
   * \param nodes vector of DRM nodes.
   * \return A string containing the node information.
   */
  static std::string get_node_info(const std::vector<std::string>& nodes);

 private:
  /**
   * \brief Retrieves tainted information.
   *
   * \return An unsigned long containing the tainted information.
   */
  static unsigned long tainted_info();

  /**
   * \brief Retrieves kernel information.
   *
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the kernel information.
   */
  [[nodiscard]] static rapidjson::Value kernel_info(
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves device information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the device information.
   */
  [[nodiscard]] static rapidjson::Value device_info(
      int fd,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves driver information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the driver information.
   */
  [[nodiscard]] static rapidjson::Value driver_info(
      int fd,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves in-formats information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param blob_id The blob ID for the in-formats.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the in-formats information.
   */
  [[nodiscard]] static rapidjson::Value in_formats_info(
      int fd,
      uint32_t blob_id,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves mode information.
   *
   * \param mode The DRM mode information.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the mode information.
   */
  static rapidjson::Value mode_info(
      const drmModeModeInfo* mode,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves mode ID information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param blob_id The blob ID for the mode ID.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the mode ID information.
   */
  [[nodiscard]] static rapidjson::Value mode_id_info(
      int fd,
      uint32_t blob_id,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves writeback pixel formats information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param blob_id The blob ID for the writeback pixel formats.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the writeback pixel formats
   * information.
   */
  [[nodiscard]] static rapidjson::Value writeback_pixel_formats_info(
      int fd,
      uint32_t blob_id,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves path information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param blob_id The blob ID for the path.
   * \return A rapidjson::Value containing the path information.
   */
  static rapidjson::Value path_info(int fd, uint32_t blob_id);

  /**
   * \brief Retrieves HDR output metadata information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param blob_id The blob ID for the HDR output metadata.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the HDR output metadata information.
   */
  [[nodiscard]] static rapidjson::Value hdr_output_metadata_info(
      int fd,
      uint32_t blob_id,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves framebuffer information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param id The framebuffer ID.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the framebuffer information.
   */
  [[nodiscard]] static rapidjson::Value
  fb_info(int fd, uint32_t id, rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves framebuffer2 information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param id The framebuffer ID.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the framebuffer2 information.
   */
  [[nodiscard]] static rapidjson::Value
  fb2_info(int fd, uint32_t id, rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves properties information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param id The properties ID.
   * \param type The properties type.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the properties information.
   */
  static rapidjson::Value properties_info(
      int fd,
      uint32_t id,
      uint32_t type,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves connectors information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param res The DRM resources.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the connectors information.
   */
  static rapidjson::Value connectors_info(
      int fd,
      const drmModeRes* res,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves encoders information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param res The DRM resources.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the encoders information.
   */
  static rapidjson::Value encoders_info(
      int fd,
      const drmModeRes* res,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves CRTCs information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param res The DRM resources.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the CRTCs information.
   */
  static rapidjson::Value crtcs_info(
      int fd,
      const drmModeRes* res,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves planes information.
   *
   * \param fd The file descriptor for the DRM device.
   * \param allocator The memory pool allocator for rapidjson.
   * \return A rapidjson::Value containing the planes information.
   */
  static rapidjson::Value planes_info(
      int fd,
      rapidjson::MemoryPoolAllocator<>& allocator);

  /**
   * \brief Retrieves node information for the given path and stores it in the
   * provided document.
   *
   * \param path The path to the DRM node.
   * \param d The rapidjson document to store the node information.
   */
  static void node_info(const char* path, rapidjson::Document& d);
};
}  // namespace drmpp::info

#endif  // INCLUDE_DRMPP_KMS_INFO_INFO_H_
