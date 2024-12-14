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

#ifndef INCLUDE_DRMPP_EGL_EGL_H
#define INCLUDE_DRMPP_EGL_EGL_H

#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>

#include "drmpp/shared_libs/libgbm.h"

namespace drmpp {

/**
 * @class Egl
 * @brief Manages EGL context and operations.
 */
class Egl {
 public:
  /**
   * @brief Constructs an Egl object.
   */
  Egl();

  /**
   * @brief Destructs the Egl object.
   */
  ~Egl();

  /**
   * @brief Sets up the EGL context.
   * @param enable_debug Enable debug mode.
   * @param render_driver Optional render driver.
   * @return True if setup is successful, false otherwise.
   */
  bool Setup(bool enable_debug, const char* render_driver = nullptr);

  /**
   * @brief Makes the current EGL context current.
   * @return True if successful, false otherwise.
   */
  [[nodiscard]] bool MakeCurrent() const;

  /**
   * @brief Creates an EGL image from a GBM buffer object.
   * @param bo GBM buffer object.
   * @return Created EGL image.
   */
  EGLImageKHR ImageCreateGbm(gbm_bo* bo) const;

  /**
   * @brief Destroys an EGL image.
   * @param image Pointer to the EGL image to be destroyed.
   */
  void ImageDestroy(EGLImageKHR* image) const;

  /**
   * @brief Creates an EGL sync object.
   * @param type Type of the sync object.
   * @param attrib_list List of attributes for the sync object.
   * @return Created EGL sync object.
   */
  EGLSyncKHR CreateSync(EGLenum type, const EGLint* attrib_list) const;

  /**
   * @brief Waits for an EGL sync object.
   * @param sync Sync object to wait for.
   * @param flags Flags for waiting.
   * @param timeout Timeout for waiting.
   * @return Result of the wait operation.
   */
  EGLint WaitSync(EGLSyncKHR sync, EGLint flags, EGLTimeKHR timeout) const;

  /**
   * @brief Destroys an EGL sync object.
   * @param sync Sync object to be destroyed.
   * @return EGLBoolean indicating success or failure.
   */
  EGLBoolean DestroySync(EGLSyncKHR sync) const;

  /**
   * @brief Checks if an extension is supported.
   * @param extension Extension to check.
   * @param extensions List of supported extensions.
   * @return True if the extension is supported, false otherwise.
   */
  static bool HasExtension(const char* extension, const char* extensions);

 private:
  struct egl_enum_item {
    EGLint id;
    const char* name;
  };
  struct egl_config_attribute {
    EGLint id;
    const char* name;
    int32_t cardinality;
    const egl_enum_item* values;
  };

  bool setup_{};                     ///< Indicates if the setup is complete.
  EGLDisplay display_;               ///< EGL display.
  EGLContext ctx_;                   ///< EGL context.
  bool use_image_flush_external_{};  ///< Indicates if image flush external is
                                     ///< used.
  bool use_dma_buf_import_modifiers_{};  ///< Indicates if DMA buffer import
                                         ///< modifiers are used.
  bool has_platform_device_{};  ///< Indicates if platform device is available.
  bool has_khr_debug_{};        ///< Indicates if KHR debug is available.
  bool has_ext_swap_buffer_with_damage_{};   ///< Indicates if EXT swap buffer
                                             ///< with damage is available.
  bool has_khr_swap_buffers_with_damage_{};  ///< Indicates if KHR swap buffers
                                             ///< with damage is available.
  bool has_khr_partial_update_{};  ///< Indicates if KHR partial update is
                                   ///< available.
  bool has_ext_buffer_age_{};  ///< Indicates if EXT buffer age is available.

  PFNEGLCREATEIMAGEKHRPROC
  CreateImageKHR_{};  ///< Function pointer for creating EGL image.
  PFNEGLDESTROYIMAGEKHRPROC
  DestroyImageKHR_{};  ///< Function pointer for destroying EGL image.
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC
  EGLImageTargetTexture2DOES_{};  ///< Function pointer for targeting
                                  ///< texture to EGL image.
  PFNEGLCREATESYNCKHRPROC
  CreateSyncKHR_{};  ///< Function pointer for creating EGL sync.
  PFNEGLCLIENTWAITSYNCKHRPROC
  ClientWaitSyncKHR_{};  ///< Function pointer for waiting for EGL sync.
  PFNEGLDESTROYSYNCKHRPROC
  DestroySyncKHR_{};  ///< Function pointer for destroying EGL sync.

  PFNEGLDEBUGMESSAGECONTROLKHRPROC
  DebugMessageControlKHR_{};  ///< Function pointer for debug message
                              ///< control.
  PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC
  SwapBuffersWithDamage_{};  ///< Function pointer for swapping buffers with
                             ///< damage.
  PFNEGLSETDAMAGEREGIONKHRPROC
  SetDamageRegionKHR_{};  ///< Function pointer for setting damage region.

  PFNEGLQUERYDEVICESEXTPROC
  QueryDevicesEXT_{};  ///< Function pointer for querying devices.
  PFNEGLQUERYDEVICESTRINGEXTPROC
  QueryDeviceStringEXT_{};  ///< Function pointer for querying device
                            ///< string.
  PFNEGLGETPLATFORMDISPLAYEXTPROC
  GetPlatformDisplayEXT_{};  ///< Function pointer for getting platform
                             ///< display.

  /**
   * @brief Assigns function pointers based on the available extensions.
   * @param extensions List of supported extensions.
   */
  void AssignFunctionPointers(const char* extensions);

  /**
   * @brief Terminates the EGL display.
   */
  void terminate_display();

  /**
   * @brief Checks the DRM driver.
   * @param device_file_name Device file name.
   * @param driver Driver name.
   * @return Result of the check.
   */
  static int CheckDrmDriver(const char* device_file_name, const char* driver);

  /**
   * @brief Gets the EGL error as a string.
   * @return EGL error string.
   */
  static const char* GetEglError();

  /**
   * @brief Prints the list of supported extensions.
   * @param dpy EGL display.
   */
  static void print_extension_list(EGLDisplay dpy);

  /**
   * @brief Reports the EGL attributes.
   * @param display EGL display.
   * @param configs List of EGL configurations.
   * @param count Number of configurations.
   */
  static void report_egl_attributes(EGLDisplay display,
                                    EGLConfig* configs,
                                    EGLint count);

  /**
   * @brief Initializes KHR debug.
   */
  void khr_debug_init() const;
};

}  // namespace drmpp

#endif  // INCLUDE_DRMPP_EGL_EGL_H