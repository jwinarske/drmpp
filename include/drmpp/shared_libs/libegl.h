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

#ifndef INCLUDE_DRMPP_SHARED_LIBS_LIBEGL_H
#define INCLUDE_DRMPP_SHARED_LIBS_LIBEGL_H

#include <EGL/egl.h>

struct LibEglExports {
  LibEglExports() = default;
  explicit LibEglExports(void* lib);

  typedef void* (*EglGetProcAddress)(const char*);
  typedef EGLBoolean (*EglInitialize)(EGLDisplay dpy,
                                      EGLint* major,
                                      EGLint* minor);
  typedef EGLDisplay (*EglGetDisplay)(EGLNativeDisplayType display_id);
  typedef EGLBoolean (*EglBindAPI)(EGLenum api);
  typedef EGLBoolean (*EglGetConfigs)(EGLDisplay dpy,
                                      EGLConfig* configs,
                                      EGLint config_size,
                                      EGLint* num_config);
  typedef EGLBoolean (*EglGetConfigAttrib)(EGLDisplay dpy,
                                           EGLConfig config,
                                           EGLint attribute,
                                           EGLint* value);
  typedef EGLBoolean (*EglChooseConfig)(EGLDisplay dpy,
                                        const EGLint* attrib_list,
                                        EGLConfig* configs,
                                        EGLint config_size,
                                        EGLint* num_config);
  typedef EGLContext (*EglCreateContext)(EGLDisplay dpy,
                                         EGLConfig config,
                                         EGLContext share_context,
                                         const EGLint* attrib_list);
  typedef EGLSurface (*EglCreateWindowSurface)(EGLDisplay dpy,
                                               EGLConfig config,
                                               EGLNativeWindowType win,
                                               const EGLint* attrib_list);
  typedef EGLBoolean (*EglMakeCurrent)(EGLDisplay dpy,
                                       EGLSurface draw,
                                       EGLSurface read,
                                       EGLContext ctx);
  typedef EGLBoolean (*EglDestroySurface)(EGLDisplay dpy, EGLSurface surface);
  typedef EGLBoolean (*EglDestroyContext)(EGLDisplay dpy, EGLContext ctx);
  typedef EGLBoolean (*EglSwapBuffers)(EGLDisplay dpy, EGLSurface surface);
  typedef EGLBoolean (*EglTerminate)(EGLDisplay dpy);

  EglGetProcAddress get_proc_address = nullptr;
  EglInitialize initialize = nullptr;
  EglGetDisplay get_display = nullptr;
  EglBindAPI bind_api = nullptr;
  EglGetConfigs get_configs = nullptr;
  EglGetConfigAttrib get_config_attrib = nullptr;
  EglChooseConfig choose_config = nullptr;
  EglCreateContext create_context = nullptr;
  EglCreateWindowSurface create_window_surface = nullptr;
  EglMakeCurrent make_current = nullptr;
  EglDestroySurface destroy_surface = nullptr;
  EglDestroyContext destroy_context = nullptr;
  EglTerminate terminate = nullptr;
  EglSwapBuffers swap_buffers = nullptr;
};

class LibEgl {
 public:
  static bool IsPresent(const char* library_path = nullptr) {
    return loadExports(library_path) != nullptr;
  }

  LibEglExports* operator->() const;

 private:
  static LibEglExports* loadExports(const char* library_path);
};

extern LibEgl LibEgl;

#endif  // INCLUDE_DRMPP_SHARED_LIBS_LIBEGL_H