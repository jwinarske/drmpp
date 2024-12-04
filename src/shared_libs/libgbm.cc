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

#include "shared_libs/libgbm.h"
#include "shared_libs/shared_library.h"

LibGbmExports::LibGbmExports(void* lib) {
  if (lib != nullptr) {
    GetFuncAddress(lib, "gbm_bo_create", &bo_create);
    GetFuncAddress(lib, "gbm_bo_destroy", &bo_destroy);
    GetFuncAddress(lib, "gbm_bo_create_with_modifiers2",
                   &bo_create_with_modifiers2);
    GetFuncAddress(lib, "gbm_bo_map", &bo_map);
    GetFuncAddress(lib, "gbm_bo_unmap", &bo_unmap);
    GetFuncAddress(lib, "gbm_bo_get_handle_for_plane",
                   &bo_get_handle_for_plane);
    GetFuncAddress(lib, "gbm_bo_get_plane_count", &bo_get_plane_count);
    GetFuncAddress(lib, "gbm_bo_get_offset", &bo_get_offset);
    GetFuncAddress(lib, "gbm_bo_get_stride_for_plane",
                   &bo_get_stride_for_plane);
    GetFuncAddress(lib, "gbm_bo_get_width", &bo_get_width);
    GetFuncAddress(lib, "gbm_bo_get_height", &bo_get_height);
    GetFuncAddress(lib, "gbm_bo_get_format", &bo_get_format);
    GetFuncAddress(lib, "gbm_bo_get_modifier", &bo_get_modifier);
    GetFuncAddress(lib, "gbm_bo_get_stride", &bo_get_stride);
    GetFuncAddress(lib, "gbm_bo_get_handle", &bo_get_handle);

    GetFuncAddress(lib, "gbm_create_device", &create_device);
    GetFuncAddress(lib, "gbm_device_destroy", &device_destroy);

    GetFuncAddress(lib, "gbm_surface_create", &surface_create);
    GetFuncAddress(lib, "gbm_surface_destroy", &surface_destroy);
    GetFuncAddress(lib, "gbm_surface_lock_front_buffer",
                   &surface_lock_front_buffer);
    GetFuncAddress(lib, "gbm_surface_release_buffer", &surface_release_buffer);
  }
}

LibGbmExports* gbm::operator->() const {
  return loadExports(nullptr);
}

LibGbmExports* gbm::loadExports(const char* library_path = nullptr) {
  static LibGbmExports exports = [&] {
    void* lib = dlopen(library_path ? library_path : "libgbm.so",
                       RTLD_LAZY | RTLD_LOCAL);
    return LibGbmExports(lib);
  }();

  return exports.bo_create ? &exports : nullptr;
}

class gbm gbm;
