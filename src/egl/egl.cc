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

#include "drmpp/egl/egl.h"

#include <fcntl.h>
#include <unistd.h>
#include <xf86drm.h>

#include <cstring>

#include "drmpp/logging/logging.h"
#include "drmpp/shared_libs/libegl.h"

namespace drmpp {
Egl::Egl() : display_(EGL_NO_DISPLAY), ctx_(EGL_NO_CONTEXT) {}

Egl::~Egl() {
  if (ctx_ != EGL_NO_CONTEXT) {
    if (display_ == EGL_NO_DISPLAY) {
      LOG_ERROR("Display is not initialized");
    }
    egl->MakeCurrent(display_, nullptr, nullptr, nullptr);
    egl->DestroyContext(display_, ctx_);
  }
  if (display_ != EGL_NO_DISPLAY)
    egl->Terminate(display_);
}

int Egl::CheckDrmDriver(const char* device_file_name, const char* driver) {
  const int fd = open(device_file_name, O_RDWR);
  if (fd < 0)
    return -1;
  const auto version = drmGetVersion(fd);
  if (!version) {
    close(fd);
    return -1;
  }
  if (strcmp(driver, version->name) == 0) {
    drmFreeVersion(version);
    close(fd);
    return 1;
  }
  drmFreeVersion(version);
  close(fd);
  return 0;
}

void Egl::terminate_display() {
  egl->Terminate(display_);
  display_ = EGL_NO_DISPLAY;
}

void Egl::AssignFunctionPointers(const char* extensions) {
  CreateImageKHR_ = reinterpret_cast<PFNEGLCREATEIMAGEKHRPROC>(
      egl->GetProcAddress("eglCreateImageKHR"));
  DestroyImageKHR_ = reinterpret_cast<PFNEGLDESTROYIMAGEKHRPROC>(
      egl->GetProcAddress("eglDestroyImageKHR"));
  EGLImageTargetTexture2DOES_ =
      reinterpret_cast<PFNGLEGLIMAGETARGETTEXTURE2DOESPROC>(
          egl->GetProcAddress("glEGLImageTargetTexture2DOES"));
  CreateSyncKHR_ = reinterpret_cast<PFNEGLCREATESYNCKHRPROC>(
      egl->GetProcAddress("eglCreateSyncKHR"));
  ClientWaitSyncKHR_ = reinterpret_cast<PFNEGLCLIENTWAITSYNCKHRPROC>(
      egl->GetProcAddress("eglClientWaitSyncKHR"));
  DestroySyncKHR_ = reinterpret_cast<PFNEGLDESTROYSYNCKHRPROC>(
      egl->GetProcAddress("eglDestroySyncKHR"));

  if (HasExtension("EGL_EXT_device_base", extensions) &&
      HasExtension("EGL_EXT_device_enumeration", extensions) &&
      HasExtension("EGL_EXT_device_query", extensions) &&
      HasExtension("EGL_EXT_platform_base", extensions) &&
      HasExtension("EGL_EXT_platform_device", extensions)) {
    QueryDevicesEXT_ = reinterpret_cast<PFNEGLQUERYDEVICESEXTPROC>(
        egl->GetProcAddress("eglQueryDevicesEXT"));
    QueryDeviceStringEXT_ = reinterpret_cast<PFNEGLQUERYDEVICESTRINGEXTPROC>(
        egl->GetProcAddress("eglQueryDeviceStringEXT"));
    GetPlatformDisplayEXT_ = reinterpret_cast<PFNEGLGETPLATFORMDISPLAYEXTPROC>(
        egl->GetProcAddress("eglGetPlatformDisplayEXT"));
    has_platform_device_ = true;
  } else {
    has_platform_device_ = false;
  }

#if defined(EGL_KHR_debug)
  if (HasExtension("EGL_KHR_debug", extensions)) {
    DebugMessageControlKHR_ =
        reinterpret_cast<PFNEGLDEBUGMESSAGECONTROLKHRPROC>(
            egl->GetProcAddress("eglDebugMessageControlKHR"));
  }
#endif
}

bool Egl::Setup(const bool enable_debug, const char* render_driver) {
  if (setup_) {
    LOG_WARN("EGL already setup");
    return true;
  }

  const char* egl_extensions = egl->QueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  if (egl_extensions) {
    AssignFunctionPointers(egl_extensions);
  }

  if (!CreateImageKHR_ || !DestroyImageKHR_ || !EGLImageTargetTexture2DOES_) {
    LOG_ERROR(
        "eglGetProcAddress returned NULL for a required extension entry "
        "point.");
    return false;
  }

  if (render_driver && !has_platform_device_) {
    LOG_ERROR("requested driver {} but required extensions are missing",
              render_driver);
    return false;
  }

  display_ = EGL_NO_DISPLAY;
  if (!has_platform_device_ || !render_driver) {
    if (GetPlatformDisplayEXT_) {
      EGLDeviceEXT devices[5];
      EGLint num_devices;
      if (0 == QueryDevicesEXT_(5, devices, &num_devices)) {
        LOG_ERROR("failed to get egl display");
        return false;
      }
      display_ =
          GetPlatformDisplayEXT_(EGL_PLATFORM_DEVICE_EXT, devices[0], nullptr);
    } else {
      display_ = egl->GetDisplay(EGL_DEFAULT_DISPLAY);
    }
  } else {
    EGLDeviceEXT devices[DRM_MAX_MINOR];
    EGLint num_devices = 0;
    EGLint d;
    QueryDevicesEXT_(DRM_MAX_MINOR, devices, &num_devices);
    for (d = 0; d < num_devices; d++) {
      const char* fn =
          QueryDeviceStringEXT_(devices[d], EGL_DRM_DEVICE_FILE_EXT);
      // We could query if display has EGL_EXT_device_drm. Or we can be lazy and
      // just query the device string and ignore the devices where it fails.
      if (!fn) {
        continue;
      }
      if (CheckDrmDriver(fn, render_driver) == 1) {
        display_ = GetPlatformDisplayEXT_(EGL_PLATFORM_DEVICE_EXT, devices[d],
                                          nullptr);
        break;
      }
    }
    if (d == num_devices)
      LOG_ERROR("failed to find requested EGL driver {}.", render_driver);
  }
  if (display_ == EGL_NO_DISPLAY) {
    LOG_ERROR("failed to get egl display");
    return false;
  }

  if (enable_debug) {
    khr_debug_init();
  }

  if (!egl->Initialize(display_, nullptr, nullptr)) {
    LOG_ERROR("failed to initialize egl: {}", GetEglError());
    return false;
  }

  // Get any EGLConfig. We need one to create a context, but it isn't used to
  // create any surfaces.
  constexpr EGLint config_attribs[] = {
      //clang-format off
      EGL_SURFACE_TYPE, EGL_DONT_CARE, EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
      EGL_NONE
      //clang-format on
  };

  EGLConfig egl_config;
  EGLint num_configs;
  if (!egl->ChooseConfig(display_, config_attribs, &egl_config, 1,
                         &num_configs)) {
    LOG_ERROR("eglChooseConfig() failed with error: {}", GetEglError());
    terminate_display();
    return false;
  }

  if (!egl->BindAPI(EGL_OPENGL_ES_API)) {
    LOG_ERROR("failed to bind OpenGL ES: {}", GetEglError());
    terminate_display();
    return false;
  }

  constexpr EGLint context_attribs[3] = {
      //clang-format off
      EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE
      //clang-format on
  };
  ctx_ =
      egl->CreateContext(display_, egl_config, EGL_NO_CONTEXT, context_attribs);
  if (ctx_ == EGL_NO_CONTEXT) {
    LOG_ERROR("failed to create OpenGL ES Context: {}", GetEglError());
    terminate_display();
    return false;
  }

  if (!egl->MakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_)) {
    LOG_ERROR("failed to make the OpenGL ES Context current: {}",
              GetEglError());
    egl->DestroyContext(display_, ctx_);
    terminate_display();
    return false;
  }

  egl_extensions = egl->QueryString(display_, EGL_EXTENSIONS);

  if (!HasExtension("EGL_KHR_image_base", egl_extensions)) {
    LOG_ERROR("EGL_KHR_image_base extension not supported");
    egl->DestroyContext(display_, ctx_);
    terminate_display();
    return false;
  }
  if (!HasExtension("EGL_EXT_image_dma_buf_import", egl_extensions)) {
    LOG_ERROR("EGL_EXT_image_dma_buf_import extension not supported");
    egl->DestroyContext(display_, ctx_);
    terminate_display();
    return false;
  }
  if (!HasExtension("EGL_KHR_fence_sync", egl_extensions) &&
      !HasExtension("EGL_KHR_wait_sync", egl_extensions)) {
    LOG_ERROR(
        "EGL_KHR_fence_sync and EGL_KHR_wait_sync extension not supported");
    egl->DestroyContext(display_, ctx_);
    terminate_display();
    return false;
  }
  use_dma_buf_import_modifiers_ =
      HasExtension("EGL_EXT_image_dma_buf_import_modifiers", egl_extensions);

#if defined(EGL_EXT_swap_buffers_with_damage) && \
    defined(EGL_KHR_swap_buffers_with_damage)
  if (HasExtension("EGL_EXT_swap_buffers_with_damage", egl_extensions)) {
    SwapBuffersWithDamage_ =
        reinterpret_cast<PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC>(
            egl->GetProcAddress("eglSwapBuffersWithDamageEXT"));
  } else {
    if (HasExtension("EGL_KHR_swap_buffers_with_damage", egl_extensions)) {
      SwapBuffersWithDamage_ =
          reinterpret_cast<PFNEGLSWAPBUFFERSWITHDAMAGEEXTPROC>(
              egl->GetProcAddress("eglSwapBuffersWithDamageKHR"));
    }
  }
#endif
#if defined(EGL_KHR_partial_update)
  if (HasExtension("EGL_KHR_partial_update", egl_extensions)) {
    SetDamageRegionKHR_ = reinterpret_cast<PFNEGLSETDAMAGEREGIONKHRPROC>(
        egl->GetProcAddress("eglSetDamageRegionKHR"));
  }
#endif
#if defined(EGL_EXT_buffer_age)
  if (HasExtension("EGL_EXT_buffer_age", egl_extensions)) {
    has_ext_buffer_age_ = true;
  }
#endif

  setup_ = true;
  return true;
}

const char* Egl::GetEglError() {
  switch (egl->GetError()) {
    case EGL_SUCCESS:
      return "EGL_SUCCESS";
    case EGL_NOT_INITIALIZED:
      return "EGL_NOT_INITIALIZED";
    case EGL_BAD_ACCESS:
      return "EGL_BAD_ACCESS";
    case EGL_BAD_ALLOC:
      return "EGL_BAD_ALLOC";
    case EGL_BAD_ATTRIBUTE:
      return "EGL_BAD_ATTRIBUTE";
    case EGL_BAD_CONTEXT:
      return "EGL_BAD_CONTEXT";
    case EGL_BAD_CONFIG:
      return "EGL_BAD_CONFIG";
    case EGL_BAD_CURRENT_SURFACE:
      return "EGL_BAD_CURRENT_SURFACE";
    case EGL_BAD_DISPLAY:
      return "EGL_BAD_DISPLAY";
    case EGL_BAD_SURFACE:
      return "EGL_BAD_SURFACE";
    case EGL_BAD_MATCH:
      return "EGL_BAD_MATCH";
    case EGL_BAD_PARAMETER:
      return "EGL_BAD_PARAMETER";
    case EGL_BAD_NATIVE_PIXMAP:
      return "EGL_BAD_NATIVE_PIXMAP";
    case EGL_BAD_NATIVE_WINDOW:
      return "EGL_BAD_NATIVE_WINDOW";
    case EGL_CONTEXT_LOST:
      return "EGL_CONTEXT_LOST";
    default:
      return "EGL_???";
  }
}

bool Egl::MakeCurrent() const {
  return egl->MakeCurrent(display_, EGL_NO_SURFACE, EGL_NO_SURFACE, ctx_);
}

EGLImageKHR Egl::ImageCreateGbm(gbm_bo* bo) const {
  int fds[GBM_MAX_PLANES];
  for (int plane = 0; plane < gbm->bo_get_plane_count(bo); plane++) {
    fds[plane] = gbm->bo_get_fd_for_plane(bo, plane);
    if (fds[plane] < 0) {
      LOG_ERROR("failed to get fb for bo: {}", fds[plane]);
      return EGL_NO_IMAGE_KHR;
    }
  }
  // When the bo has 3 planes with modifier support, it requires 39 components.
  EGLint khr_image_attrs[39] = {
      //clang-format off
      EGL_WIDTH,
      static_cast<EGLint>(gbm->bo_get_width(bo)),
      EGL_HEIGHT,
      static_cast<EGLint>(gbm->bo_get_height(bo)),
      EGL_LINUX_DRM_FOURCC_EXT,
      static_cast<int>(gbm->bo_get_format(bo)),
      EGL_NONE,
      //clang-format on
  };
  size_t attrs_index = 6;
  for (int plane = 0; plane < gbm->bo_get_plane_count(bo); plane++) {
    khr_image_attrs[attrs_index++] = EGL_DMA_BUF_PLANE0_FD_EXT + plane * 3;
    khr_image_attrs[attrs_index++] = fds[plane];
    khr_image_attrs[attrs_index++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT + plane * 3;
    khr_image_attrs[attrs_index++] =
        static_cast<EGLint>(gbm->bo_get_offset(bo, plane));
    khr_image_attrs[attrs_index++] = EGL_DMA_BUF_PLANE0_PITCH_EXT + plane * 3;
    khr_image_attrs[attrs_index++] =
        static_cast<EGLint>(gbm->bo_get_stride_for_plane(bo, plane));
    if (use_dma_buf_import_modifiers_) {
      const uint64_t modifier = gbm->bo_get_modifier(bo);
      khr_image_attrs[attrs_index++] =
          EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT + plane * 2;
      khr_image_attrs[attrs_index++] =
          static_cast<EGLint>(modifier & 0xfffffffful);
      khr_image_attrs[attrs_index++] =
          EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT + plane * 2;
      khr_image_attrs[attrs_index++] = static_cast<EGLint>(modifier >> 32);
    }
  }
  khr_image_attrs[attrs_index] = EGL_NONE;

  const auto image =
      CreateImageKHR_(display_, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                      nullptr /* no client buffer */, khr_image_attrs);
  if (image == EGL_NO_IMAGE_KHR) {
    LOG_ERROR("failed to make image from target buffer: {}", GetEglError());
    return EGL_NO_IMAGE_KHR;
  }
  for (int plane = 0; plane < gbm->bo_get_plane_count(bo); plane++) {
    close(fds[plane]);
  }
  return image;
}

void Egl::ImageDestroy(EGLImageKHR* image) const {
  DestroyImageKHR_(display_, *image);
  *image = EGL_NO_IMAGE_KHR;
}

EGLSyncKHR Egl::CreateSync(const EGLenum type,
                           const EGLint* attrib_list) const {
  return CreateSyncKHR_(display_, type, attrib_list);
}

EGLint Egl::WaitSync(EGLSyncKHR sync,
                     const EGLint flags,
                     EGLTimeKHR timeout) const {
  return ClientWaitSyncKHR_(display_, sync, flags, timeout);
}

EGLBoolean Egl::DestroySync(EGLSyncKHR sync) const {
  return DestroySyncKHR_(display_, sync);
}

bool Egl::HasExtension(const char* extension, const char* extensions) {
  const char* start = extensions;
  const size_t ext_len = std::strlen(extension);
  while ((start = strstr(start, extension)) != nullptr) {
    const char* end = start + ext_len;
    if ((start == extensions || *(start - 1) == ' ') &&
        (*end == ' ' || *end == '\0')) {
      return true;
    }
    start = end;
  }
  return false;
}

void Egl::print_extension_list(EGLDisplay dpy) {
  constexpr char indentString[] = "\t    ";
  constexpr int indent = 4;
  const char* ext = egl->QueryString(dpy, EGL_EXTENSIONS);
  if (!ext || !ext[0])
    return;

  int width = indent;
  std::string line = indentString;
  for (int i = 0, j = 0; ext[j] != '\0'; j++) {
    if (ext[j] == ' ') {
      const int len = j - i;
      if (width + len > 79) {
        LOG_INFO(line.c_str());
        line = indentString;
        width = indent;
      }
      line.append(ext + i, len);
      width += len + 1;
      i = j + 1;
      line.append(", ");
      width += 2;
    }
  }
  LOG_INFO(line.c_str());
}

void Egl::report_egl_attributes(EGLDisplay display,
                                EGLConfig* configs,
                                const EGLint count) {
  static constexpr egl_enum_item egl_enum_boolean[] = {
      {EGL_TRUE, "EGL_TRUE"},
      {EGL_FALSE, "EGL_FALSE"},
  };
  static constexpr egl_enum_item egl_enum_caveat[] = {
      {EGL_NONE, "EGL_NONE"},
      {EGL_SLOW_CONFIG, "EGL_SLOW_CONFIG"},
      {EGL_NON_CONFORMANT_CONFIG, "EGL_NON_CONFORMANT_CONFIG"},
  };
  static constexpr egl_enum_item egl_enum_transparency[] = {
      {EGL_NONE, "EGL_NONE"},
      {EGL_TRANSPARENT_RGB, "EGL_TRANSPARENT_RGB"},
  };
  static constexpr egl_enum_item egl_enum_color_buffer[] = {
      {EGL_RGB_BUFFER, "EGL_RGB_BUFFER"},
      {EGL_LUMINANCE_BUFFER, "EGL_LUMINANCE_BUFFER"},
  };
  static constexpr egl_enum_item egl_enum_conformant[] = {
      {EGL_OPENGL_BIT, "EGL_OPENGL_BIT"},
      {EGL_OPENGL_ES_BIT, "EGL_OPENGL_ES_BIT"},
      {EGL_OPENGL_ES2_BIT, "EGL_OPENGL_ES2_BIT"},
#ifdef EGL_OPENGL_ES3_BIT
      {EGL_OPENGL_ES3_BIT, "EGL_OPENGL_ES3_BIT"},
#endif
#ifdef EGL_OPENVG_BIT
      {EGL_OPENVG_BIT, "EGL_OPENVG_BIT"},
#endif
  };
  static constexpr egl_enum_item egl_enum_surface_type[] = {
      {EGL_PBUFFER_BIT, "EGL_PBUFFER_BIT"},
      {EGL_PIXMAP_BIT, "EGL_PIXMAP_BIT"},
      {EGL_WINDOW_BIT, "EGL_WINDOW_BIT"},
      {EGL_VG_COLORSPACE_LINEAR_BIT, "EGL_VG_COLORSPACE_LINEAR_BIT"},
      {EGL_VG_ALPHA_FORMAT_PRE_BIT, "EGL_VG_ALPHA_FORMAT_PRE_BIT"},
      {EGL_MULTISAMPLE_RESOLVE_BOX_BIT, "EGL_MULTISAMPLE_RESOLVE_BOX_BIT"},
      {EGL_SWAP_BEHAVIOR_PRESERVED_BIT, "EGL_SWAP_BEHAVIOR_PRESERVED_BIT"},
  };
  static constexpr egl_enum_item egl_enum_renderable_type[] = {
      {EGL_OPENGL_ES_BIT, "EGL_OPENGL_ES_BIT"},
      {EGL_OPENVG_BIT, "EGL_OPENVG_BIT"},
      {EGL_OPENGL_ES2_BIT, "EGL_OPENGL_ES2_BIT"},
      {EGL_OPENGL_BIT, "EGL_OPENGL_BIT"},
#ifdef EGL_OPENGL_ES3_BIT
      {EGL_OPENGL_ES3_BIT, "EGL_OPENGL_ES3_BIT"},
#endif
  };
  static constexpr egl_config_attribute egl_config_attributes[] = {
      {EGL_CONFIG_ID, "EGL_CONFIG_ID"},
      {EGL_CONFIG_CAVEAT, "EGL_CONFIG_CAVEAT", std::size(egl_enum_caveat),
       egl_enum_caveat},
      {EGL_LUMINANCE_SIZE, "EGL_LUMINANCE_SIZE"},
      {EGL_RED_SIZE, "EGL_RED_SIZE"},
      {EGL_GREEN_SIZE, "EGL_GREEN_SIZE"},
      {EGL_BLUE_SIZE, "EGL_BLUE_SIZE"},
      {EGL_ALPHA_SIZE, "EGL_ALPHA_SIZE"},
      {EGL_DEPTH_SIZE, "EGL_DEPTH_SIZE"},
      {EGL_STENCIL_SIZE, "EGL_STENCIL_SIZE"},
      {EGL_ALPHA_MASK_SIZE, "EGL_ALPHA_MASK_SIZE"},
      {EGL_BIND_TO_TEXTURE_RGB, "EGL_BIND_TO_TEXTURE_RGB",
       std::size(egl_enum_boolean), egl_enum_boolean},
      {EGL_BIND_TO_TEXTURE_RGBA, "EGL_BIND_TO_TEXTURE_RGBA",
       std::size(egl_enum_boolean), egl_enum_boolean},
      {EGL_MAX_PBUFFER_WIDTH, "EGL_MAX_PBUFFER_WIDTH"},
      {EGL_MAX_PBUFFER_HEIGHT, "EGL_MAX_PBUFFER_HEIGHT"},
      {EGL_MAX_PBUFFER_PIXELS, "EGL_MAX_PBUFFER_PIXELS"},
      {EGL_TRANSPARENT_RED_VALUE, "EGL_TRANSPARENT_RED_VALUE"},
      {EGL_TRANSPARENT_GREEN_VALUE, "EGL_TRANSPARENT_GREEN_VALUE"},
      {EGL_TRANSPARENT_BLUE_VALUE, "EGL_TRANSPARENT_BLUE_VALUE"},
      {EGL_SAMPLE_BUFFERS, "EGL_SAMPLE_BUFFERS"},
      {EGL_SAMPLES, "EGL_SAMPLES"},
      {EGL_LEVEL, "EGL_LEVEL"},
      {EGL_MAX_SWAP_INTERVAL, "EGL_MAX_SWAP_INTERVAL"},
      {EGL_MIN_SWAP_INTERVAL, "EGL_MIN_SWAP_INTERVAL"},
      {EGL_SURFACE_TYPE, "EGL_SURFACE_TYPE",
       -static_cast<int32_t>(std::size(egl_enum_surface_type)),
       egl_enum_surface_type},
      {EGL_RENDERABLE_TYPE, "EGL_RENDERABLE_TYPE",
       -static_cast<int32_t>(std::size(egl_enum_renderable_type)),
       egl_enum_renderable_type},
      {EGL_CONFORMANT, "EGL_CONFORMANT",
       -static_cast<int32_t>(std::size(egl_enum_conformant)),
       egl_enum_conformant},
      {EGL_TRANSPARENT_TYPE, "EGL_TRANSPARENT_TYPE",
       std::size(egl_enum_transparency), egl_enum_transparency},
      {EGL_COLOR_BUFFER_TYPE, "EGL_COLOR_BUFFER_TYPE",
       std::size(egl_enum_color_buffer), egl_enum_color_buffer},
  };

  LOG_INFO("EGL Attributes:");
  LOG_INFO("\tEGL_VENDOR: \"{}\"", egl->QueryString(display, EGL_VENDOR));
  LOG_INFO("\tEGL_CLIENT_APIS: \"{}\"",
           egl->QueryString(display, EGL_CLIENT_APIS));
  LOG_INFO("\tEGL_EXTENSIONS:");
  print_extension_list(display);

  EGLint num_config;
  if (egl->GetConfigs(display, configs, count, &num_config) != EGL_TRUE ||
      num_config == 0) {
    LOG_ERROR("failed to get EGL frame buffer configurations");
    return;
  }

  LOG_INFO("EGL framebuffer configurations:");
  for (EGLint i = 0; i < num_config; i++) {
    LOG_INFO("\tConfiguration #{}", i);
    for (auto& [id, name, cardinality, values] : egl_config_attributes) {
      EGLint value = 0;
      egl->GetConfigAttrib(display, configs[i], id, &value);
      if (cardinality == 0) {
        LOG_INFO("\t\t{}: {}", name, value);
      } else if (cardinality > 0) {
        bool known_value = false;
        for (size_t k = 0; k < static_cast<size_t>(cardinality); k++) {
          if (values[k].id == value) {
            LOG_INFO("\t\t{}: {}", name, values[k].name);
            known_value = true;
            break;
          }
        }
        if (!known_value) {
          LOG_INFO("\t\t{}: unknown ({})", name, value);
        }
      } else {
        LOG_INFO("\t\t{}: {}", name, value == 0 ? "none" : "");
        for (size_t k = 0; k < static_cast<size_t>(-cardinality); k++) {
          if (values[k].id & value) {
            value &= ~values[k].id;
            LOG_INFO("\t\t{}: {}", name, values[k].name);
          }
        }
        if (value != 0) {
          LOG_INFO("\t\t{}: {}", name, value);
        }
      }
    }
  }
}

void Egl::khr_debug_init() const {
  if (!DebugMessageControlKHR_) {
    return;
  }
  constexpr EGLAttrib attrib_list[] = {
      //clang-format off
      EGL_DEBUG_MSG_CRITICAL_KHR,
      EGL_TRUE,
      EGL_DEBUG_MSG_ERROR_KHR,
      EGL_TRUE,
      EGL_DEBUG_MSG_WARN_KHR,
      EGL_TRUE,
      EGL_DEBUG_MSG_INFO_KHR,
      EGL_TRUE,
      EGL_NONE,
      EGL_NONE,
      //clang-format on
  };
  DebugMessageControlKHR_(
      [](EGLenum error, const char* command, const EGLint messageType,
         EGLLabelKHR threadLabel, EGLLabelKHR objectLabel,
         const char* message) {
        switch (messageType) {
          case EGL_DEBUG_MSG_CRITICAL_KHR:
            LOG_CRITICAL("**** EGL");
            LOG_CRITICAL("\terror: {}", error);
            LOG_CRITICAL("\tcommand: {}", command);
            LOG_CRITICAL("\tthreadLabel: {}", threadLabel);
            LOG_CRITICAL("\tobjectLabel: {}", objectLabel);
            LOG_CRITICAL("\tmessage: {}", message ? message : "");
            break;
          case EGL_DEBUG_MSG_ERROR_KHR:
            LOG_ERROR("**** EGL");
            LOG_ERROR("\terror: {}", error);
            LOG_ERROR("\tcommand: {}", command);
            LOG_ERROR("\tthreadLabel: {}", threadLabel);
            LOG_ERROR("\tobjectLabel: {}", objectLabel);
            LOG_ERROR("\tmessage: {}", message ? message : "");
            break;
          case EGL_DEBUG_MSG_WARN_KHR:
            LOG_WARN("**** EGL");
            LOG_WARN("\terror: {}", error);
            LOG_WARN("\tcommand: {}", command);
            LOG_WARN("\tthreadLabel: {}", threadLabel);
            LOG_WARN("\tobjectLabel: {}", objectLabel);
            LOG_WARN("\tmessage: {}", message ? message : "");
            break;
          default:
          case EGL_DEBUG_MSG_INFO_KHR:
            LOG_INFO("**** EGL");
            LOG_INFO("\terror: {}", error);
            LOG_INFO("\tcommand: {}", command);
            LOG_INFO("\tthreadLabel: {}", threadLabel);
            LOG_INFO("\tobjectLabel: {}", objectLabel);
            LOG_INFO("\tmessage: {}", message ? message : "");
            break;
        }
      },
      attrib_list);
}
}  // namespace drmpp