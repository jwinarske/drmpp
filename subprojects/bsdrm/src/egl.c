/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright 2016 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */

#include "bs_drm.h"

static const char *get_egl_error();

static const char *get_gl_framebuffer_error();

struct bs_egl {
  bool setup;
  EGLDisplay display;
  EGLContext ctx;
  bool use_image_flush_external;
  bool use_dma_buf_import_modifiers;
  bool has_platform_device;
  // Names are the original gl/egl function names with the prefix chopped off.
  PFNEGLCREATEIMAGEKHRPROC CreateImageKHR;
  PFNEGLDESTROYIMAGEKHRPROC DestroyImageKHR;
  PFNGLEGLIMAGETARGETTEXTURE2DOESPROC EGLImageTargetTexture2DOES;
  PFNEGLCREATESYNCKHRPROC CreateSyncKHR;
  PFNEGLCLIENTWAITSYNCKHRPROC ClientWaitSyncKHR;
  PFNEGLDESTROYSYNCKHRPROC DestroySyncKHR;
  PFNEGLQUERYDEVICESEXTPROC QueryDevicesEXT;
  PFNEGLQUERYDEVICESTRINGEXTPROC QueryDeviceStringEXT;
  PFNEGLGETPLATFORMDISPLAYEXTPROC GetPlatformDisplayEXT;
};

struct bs_egl_fb {
  GLuint tex;
  GLuint fb;
};

struct bs_egl *bs_egl_new() {
  struct bs_egl *self = calloc(1, sizeof(struct bs_egl));
  assert(self);
  self->display = EGL_NO_DISPLAY;
  self->ctx = EGL_NO_CONTEXT;
  return self;
}

void bs_egl_destroy(struct bs_egl **egl) {
  assert(egl);
  struct bs_egl *self = *egl;
  assert(self);
  if (self->ctx != EGL_NO_CONTEXT) {
    assert(self->display != EGL_NO_DISPLAY);
    eglMakeCurrent(self->display, NULL, NULL, NULL);
    eglDestroyContext(self->display, self->ctx);
  }
  if (self->display != EGL_NO_DISPLAY)
    eglTerminate(self->display);
  free(self);
  *egl = NULL;
}

static int bs_check_drm_driver(const char *device_file_name,
                               const char *driver) {
  drmVersionPtr version;
  int fd = open(device_file_name, O_RDWR);
  if (fd < 0)
    return -1;
  version = drmGetVersion(fd);
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

bool bs_egl_setup(struct bs_egl *self, const char *render_driver) {
  assert(self);
  assert(!self->setup);
  const char *egl_client_extensions =
      eglQueryString(EGL_NO_DISPLAY, EGL_EXTENSIONS);
  self->CreateImageKHR =
      (PFNEGLCREATEIMAGEKHRPROC) eglGetProcAddress("eglCreateImageKHR");
  self->DestroyImageKHR =
      (PFNEGLDESTROYIMAGEKHRPROC) eglGetProcAddress("eglDestroyImageKHR");
  self->EGLImageTargetTexture2DOES =
      (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC) eglGetProcAddress(
        "glEGLImageTargetTexture2DOES");
  if (!self->CreateImageKHR || !self->DestroyImageKHR ||
      !self->EGLImageTargetTexture2DOES) {
    bs_debug_error(
      "eglGetProcAddress returned NULL for a required extension entry "
      "point.");
    return false;
  }
  self->CreateSyncKHR =
      (PFNEGLCREATESYNCKHRPROC) eglGetProcAddress("eglCreateSyncKHR");
  self->ClientWaitSyncKHR =
      (PFNEGLCLIENTWAITSYNCKHRPROC) eglGetProcAddress("eglClientWaitSyncKHR");
  self->DestroySyncKHR =
      (PFNEGLDESTROYSYNCKHRPROC) eglGetProcAddress("eglDestroySyncKHR");
  if (egl_client_extensions &&
      bs_egl_has_extension("EGL_EXT_device_base", egl_client_extensions) &&
      bs_egl_has_extension("EGL_EXT_device_enumeration",
                           egl_client_extensions) &&
      bs_egl_has_extension("EGL_EXT_device_query", egl_client_extensions) &&
      bs_egl_has_extension("EGL_EXT_platform_base", egl_client_extensions) &&
      bs_egl_has_extension("EGL_EXT_platform_device", egl_client_extensions)) {
    self->QueryDevicesEXT =
        (PFNEGLQUERYDEVICESEXTPROC) eglGetProcAddress("eglQueryDevicesEXT");
    self->QueryDeviceStringEXT =
        (PFNEGLQUERYDEVICESTRINGEXTPROC) eglGetProcAddress(
          "eglQueryDeviceStringEXT");
    self->GetPlatformDisplayEXT =
        (PFNEGLGETPLATFORMDISPLAYEXTPROC) eglGetProcAddress(
          "eglGetPlatformDisplayEXT");
    self->has_platform_device = true;
  } else {
    self->has_platform_device = false;
  }
  if (render_driver && !self->has_platform_device) {
    bs_debug_error("requested driver %s but required extensions are missing",
                   render_driver);
    return false;
  }
  self->display = EGL_NO_DISPLAY;
  if (!self->has_platform_device || !render_driver) {
    if (self->GetPlatformDisplayEXT) {
      EGLDeviceEXT devices[5];
      EGLint num_devices;
      if (0 == self->QueryDevicesEXT(5, devices, &num_devices)) {
        fprintf(stderr, "failed to get egl display\n");
        return 1;
      }
      self->display = self->GetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT, devices[0], NULL);
    } else {
      self->display = eglGetDisplay(EGL_DEFAULT_DISPLAY);
    }
  } else {
    EGLDeviceEXT devices[DRM_MAX_MINOR];
    EGLint num_devices = 0;
    EGLint d;
    self->QueryDevicesEXT(DRM_MAX_MINOR, devices, &num_devices);
    for (d = 0; d < num_devices; d++) {
      const char *fn =
          self->QueryDeviceStringEXT(devices[d], EGL_DRM_DEVICE_FILE_EXT);
      // We could query if display has EGL_EXT_device_drm. Or we can be lazy and
      // just query the device string and ignore the devices where it fails.
      if (!fn) {
        continue;
      }
      if (bs_check_drm_driver(fn, render_driver) == 1) {
        self->display = self->GetPlatformDisplayEXT(EGL_PLATFORM_DEVICE_EXT,
                                                    (void *) devices[d], NULL);
        break;
      }
    }
    if (d == num_devices)
      bs_debug_error("failed to find requested EGL driver %s.", render_driver);
  }
  if (self->display == EGL_NO_DISPLAY) {
    bs_debug_error("failed to get egl display");
    return false;
  }
  if (!eglInitialize(self->display, NULL /* ignore version */,
                     NULL /* ignore version */)) {
    bs_debug_error("failed to initialize egl: %s\n", get_egl_error());
    return false;
  }
  // Get any EGLConfig. We need one to create a context, but it isn't used to
  // create any surfaces.
  const EGLint config_attribs[] = {
    EGL_SURFACE_TYPE, EGL_DONT_CARE,
    EGL_RENDERABLE_TYPE, EGL_OPENGL_ES2_BIT,
    EGL_NONE
  };
  EGLConfig egl_config;
  EGLint num_configs;
  if (!eglChooseConfig(self->display, config_attribs, &egl_config, 1,
                       &num_configs /* unused but can't be null */)) {
    bs_debug_error("eglChooseConfig() failed with error: %s", get_egl_error());
    goto terminate_display;
  }
  if (!eglBindAPI(EGL_OPENGL_ES_API)) {
    bs_debug_error("failed to bind OpenGL ES: %s", get_egl_error());
    goto terminate_display;
  }
  const EGLint context_attribs[] = {EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE};
  self->ctx =
      eglCreateContext(self->display, egl_config,
                       EGL_NO_CONTEXT /* no shared context */, context_attribs);
  if (self->ctx == EGL_NO_CONTEXT) {
    bs_debug_error("failed to create OpenGL ES Context: %s", get_egl_error());
    goto terminate_display;
  }
  if (!eglMakeCurrent(self->display,
                      EGL_NO_SURFACE /* no default draw surface */,
                      EGL_NO_SURFACE /* no default draw read */, self->ctx)) {
    bs_debug_error("failed to make the OpenGL ES Context current: %s",
                   get_egl_error());
    goto destroy_context;
  }
  const char *egl_extensions = eglQueryString(self->display, EGL_EXTENSIONS);
  if (!bs_egl_has_extension("EGL_KHR_image_base", egl_extensions)) {
    bs_debug_error("EGL_KHR_image_base extension not supported");
    goto destroy_context;
  }
  if (!bs_egl_has_extension("EGL_EXT_image_dma_buf_import", egl_extensions)) {
    bs_debug_error("EGL_EXT_image_dma_buf_import extension not supported");
    goto destroy_context;
  }
  if (!bs_egl_has_extension("EGL_KHR_fence_sync", egl_extensions) &&
      !bs_egl_has_extension("EGL_KHR_wait_sync", egl_extensions)) {
    bs_debug_error(
      "EGL_KHR_fence_sync and EGL_KHR_wait_sync extension not supported");
    goto destroy_context;
  }
  if (bs_egl_has_extension("EGL_EXT_image_dma_buf_import_modifiers",
                           egl_extensions))
    self->use_dma_buf_import_modifiers = true;
  const char *gl_extensions = (const char *) glGetString(GL_EXTENSIONS);
  if (!bs_egl_has_extension("GL_OES_EGL_image", gl_extensions)) {
    bs_debug_error("GL_OES_EGL_image extension not supported");
    goto destroy_context;
  }
  self->setup = true;
  return true;
destroy_context:
  eglDestroyContext(self->display, self->ctx);
terminate_display:
  eglTerminate(self->display);
  self->display = EGL_NO_DISPLAY;
  return false;
}

bool bs_egl_make_current(struct bs_egl *self) {
  assert(self);
  assert(self->display != EGL_NO_DISPLAY);
  assert(self->ctx != EGL_NO_CONTEXT);
  return eglMakeCurrent(self->display,
                        EGL_NO_SURFACE /* No default draw surface */,
                        EGL_NO_SURFACE /* No default draw read */, self->ctx);
}

EGLImageKHR bs_egl_image_create_gbm(struct bs_egl *self, struct gbm_bo *bo) {
  assert(self);
  assert(self->CreateImageKHR);
  assert(self->display != EGL_NO_DISPLAY);
  assert(bo);
  int fds[GBM_MAX_PLANES];
  for (size_t plane = 0; plane < gbm_bo_get_plane_count(bo); plane++) {
    fds[plane] = gbm_bo_get_fd_for_plane(bo, plane);
    if (fds[plane] < 0) {
      bs_debug_error("failed to get fb for bo: %d", fds[plane]);
      return EGL_NO_IMAGE_KHR;
    }
  }
  // When the bo has 3 planes with modifier support, it requires 39 components.
  EGLint khr_image_attrs[39] = {
    EGL_WIDTH,
    gbm_bo_get_width(bo),
    EGL_HEIGHT,
    gbm_bo_get_height(bo),
    EGL_LINUX_DRM_FOURCC_EXT,
    (int) gbm_bo_get_format(bo),
    EGL_NONE,
  };
  size_t attrs_index = 6;
  for (size_t plane = 0; plane < gbm_bo_get_plane_count(bo); plane++) {
    khr_image_attrs[attrs_index++] = EGL_DMA_BUF_PLANE0_FD_EXT + plane * 3;
    khr_image_attrs[attrs_index++] = fds[plane];
    khr_image_attrs[attrs_index++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT + plane * 3;
    khr_image_attrs[attrs_index++] = gbm_bo_get_offset(bo, plane);
    khr_image_attrs[attrs_index++] = EGL_DMA_BUF_PLANE0_PITCH_EXT + plane * 3;
    khr_image_attrs[attrs_index++] = gbm_bo_get_stride_for_plane(bo, plane);
    if (self->use_dma_buf_import_modifiers) {
      const uint64_t modifier = gbm_bo_get_modifier(bo);
      khr_image_attrs[attrs_index++] =
          EGL_DMA_BUF_PLANE0_MODIFIER_LO_EXT + plane * 2;
      khr_image_attrs[attrs_index++] = modifier & 0xfffffffful;
      khr_image_attrs[attrs_index++] =
          EGL_DMA_BUF_PLANE0_MODIFIER_HI_EXT + plane * 2;
      khr_image_attrs[attrs_index++] = modifier >> 32;
    }
  }
  khr_image_attrs[attrs_index++] = EGL_NONE;
  EGLImageKHR image =
      self->CreateImageKHR(self->display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT,
                           NULL /* no client buffer */, khr_image_attrs);
  if (image == EGL_NO_IMAGE_KHR) {
    bs_debug_error("failed to make image from target buffer: %s",
                   get_egl_error());
    return EGL_NO_IMAGE_KHR;
  }
  for (size_t plane = 0; plane < gbm_bo_get_plane_count(bo); plane++) {
    close(fds[plane]);
  }
  return image;
}

void bs_egl_image_destroy(struct bs_egl *self, EGLImageKHR *image) {
  assert(self);
  assert(image);
  assert(*image != EGL_NO_IMAGE_KHR);
  assert(self->DestroyImageKHR);
  self->DestroyImageKHR(self->display, *image);
  *image = EGL_NO_IMAGE_KHR;
}

bool bs_egl_image_flush_external(struct bs_egl *self, EGLImageKHR image) {
  assert(self);
  assert(image != EGL_NO_IMAGE_KHR);
  if (!self->use_image_flush_external)
    return true;
  return true;
}

struct bs_egl_fb *bs_egl_fb_new(struct bs_egl *self, EGLImageKHR image) {
  assert(self);
  assert(self->EGLImageTargetTexture2DOES);
  struct bs_egl_fb *fb = calloc(1, sizeof(struct bs_egl_fb));
  assert(fb);
  glGenTextures(1, &fb->tex);
  glBindTexture(GL_TEXTURE_2D, fb->tex);
  self->EGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES) image);
  glBindTexture(GL_TEXTURE_2D, 0);
  glGenFramebuffers(1, &fb->fb);
  glBindFramebuffer(GL_FRAMEBUFFER, fb->fb);
  glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D,
                         fb->tex, 0);
  if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) {
    bs_debug_error("failed framebuffer check for created target buffer: %s",
                   get_gl_framebuffer_error());
    glDeleteFramebuffers(1, &fb->fb);
    glDeleteTextures(1, &fb->tex);
    free(fb);
    return NULL;
  }
  return fb;
}

void bs_egl_fb_destroy(struct bs_egl_fb **fb) {
  assert(fb);
  struct bs_egl_fb *self = *fb;
  assert(self);
  glDeleteFramebuffers(1, &self->fb);
  glDeleteTextures(1, &self->tex);
  free(self);
  *fb = NULL;
}

GLuint bs_egl_fb_name(struct bs_egl_fb *self) {
  assert(self);
  return self->fb;
}

bool bs_egl_target_texture2D(struct bs_egl *self, EGLImageKHR image) {
  assert(self);
  assert(self->EGLImageTargetTexture2DOES);
  self->EGLImageTargetTexture2DOES(GL_TEXTURE_2D, (GLeglImageOES) image);
  GLint error = glGetError();
  return (error == GL_NO_ERROR);
}

EGLSyncKHR bs_egl_create_sync(struct bs_egl *self,
                              EGLenum type,
                              const EGLint *attrib_list) {
  return self->CreateSyncKHR(self->display, type, attrib_list);
}

EGLint bs_egl_wait_sync(struct bs_egl *self,
                        EGLSyncKHR sync,
                        EGLint flags,
                        EGLTimeKHR timeout) {
  return self->ClientWaitSyncKHR(self->display, sync, flags, timeout);
}

EGLBoolean bs_egl_destroy_sync(struct bs_egl *self, EGLSyncKHR sync) {
  return self->DestroySyncKHR(self->display, sync);
}

bool bs_egl_has_extension(const char *extension, const char *extensions) {
  const char *start, *where, *terminator;
  start = extensions;
  for (;;) {
    where = (char *) strstr((const char *) start, extension);
    if (!where)
      break;
    terminator = where + strlen(extension);
    if (where == start || *(where - 1) == ' ')
      if (*terminator == ' ' || *terminator == '\0')
        return true;
    start = terminator;
  }
  return false;
}

static const char *get_egl_error() {
  switch (eglGetError()) {
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

static const char *get_gl_framebuffer_error() {
  switch (glCheckFramebufferStatus(GL_FRAMEBUFFER)) {
    case GL_FRAMEBUFFER_COMPLETE:
      return "GL_FRAMEBUFFER_COMPLETE";
    case GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT:
      return "GL_FRAMEBUFFER_INCOMPLETE_ATTACHMENT";
    case GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT:
      return "GL_FRAMEBUFFER_INCOMPLETE_MISSING_ATTACHMENT";
    case GL_FRAMEBUFFER_UNSUPPORTED:
      return "GL_FRAMEBUFFER_UNSUPPORTED";
    case GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS:
      return "GL_FRAMEBUFFER_INCOMPLETE_DIMENSIONS";
    default:
      return "GL_FRAMEBUFFER_???";
  }
}
