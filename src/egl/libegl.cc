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

LibEglExports::LibEglExports(void *lib) {
  if (lib != nullptr) {
    GetFuncAddress(lib, "eglGetProcAddress", &GetProcAddress);
    GetFuncAddress(lib, "eglInitialize", &Initialize);
    GetFuncAddress(lib, "eglGetDisplay", &GetDisplay);
    GetFuncAddress(lib, "eglBindAPI", &BindAPI);
    GetFuncAddress(lib, "eglGetConfigs", &GetConfigs);
    GetFuncAddress(lib, "eglGetConfigAttrib", &GetConfigAttrib);
    GetFuncAddress(lib, "eglChooseConfig", &ChooseConfig);
    GetFuncAddress(lib, "eglCreateContext", &CreateContext);
    GetFuncAddress(lib, "eglCreateWindowSurface", &CreateWindowSurface);
    GetFuncAddress(lib, "eglMakeCurrent", &MakeCurrent);
    GetFuncAddress(lib, "eglDestroySurface", &DestroySurface);
    GetFuncAddress(lib, "eglDestroyContext", &DestroyContext);
    GetFuncAddress(lib, "eglSwapBuffers", &SwapBuffers);
    GetFuncAddress(lib, "eglTerminate", &Terminate);
  }
}

LibEglExports *egl::operator->() const {
  return loadExports(nullptr);
}

LibEglExports *egl::loadExports(const char *library_path = nullptr) {
  static LibEglExports exports = [&] {
    void *lib = dlopen(library_path ? library_path : "libEGL.so",
                       RTLD_LAZY | RTLD_LOCAL);
    return LibEglExports(lib);
  }();

  return exports.GetProcAddress ? &exports : nullptr;
}

class egl egl;
