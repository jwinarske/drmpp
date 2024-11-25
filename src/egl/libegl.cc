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

#include <dlfcn.h>

#include "shared_libs/libegl.h"
#include "shared_libs/shared_library.h"

LibEglExports::LibEglExports(void* lib) {
  if (lib != nullptr) {
    GetFuncAddress(lib, "eglGetProcAddress", &get_proc_address);
    GetFuncAddress(lib, "eglInitialize", &initialize);
    GetFuncAddress(lib, "eglGetDisplay", &get_display);
    GetFuncAddress(lib, "eglBindAPI", &bind_api);
    GetFuncAddress(lib, "eglGetConfigs", &get_configs);
    GetFuncAddress(lib, "eglGetConfigAttrib", &get_config_attrib);
    GetFuncAddress(lib, "eglChooseConfig", &choose_config);
    GetFuncAddress(lib, "eglCreateContext", &create_context);
    GetFuncAddress(lib, "eglCreateWindowSurface", &create_window_surface);
    GetFuncAddress(lib, "eglMakeCurrent", &make_current);
    GetFuncAddress(lib, "eglDestroySurface", &destroy_surface);
    GetFuncAddress(lib, "eglDestroyContext", &destroy_context);
    GetFuncAddress(lib, "eglSwapBuffers", &swap_buffers);
    GetFuncAddress(lib, "eglTerminate", &terminate);
  }
}

LibEglExports* LibEgl::operator->() const {
  return loadExports(nullptr);
}

LibEglExports* LibEgl::loadExports(const char* library_path = nullptr) {
  static LibEglExports exports = [&] {
    void* lib = dlopen(library_path ? library_path : "libEGL.so",
                       RTLD_LAZY | RTLD_LOCAL);
    return LibEglExports(lib);
  }();

  return exports.get_proc_address ? &exports : nullptr;
}

class LibEgl LibEgl;
