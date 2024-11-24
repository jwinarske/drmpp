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

#include "shared_libs/libdrm.h"
#include "shared_libs/shared_library.h"

LibDrmExports::LibDrmExports(void* lib) {
  if (lib != nullptr) {
    GetFuncAddress(lib, "drmIoctl", &drm_ioctl);
    GetFuncAddress(lib, "drmModeGetConnector", &mode_get_connector);
    GetFuncAddress(lib, "drmModeFreeConnector", &mode_free_connector);
    GetFuncAddress(lib, "drmModeGetEncoder", &mode_get_encoder);
    GetFuncAddress(lib, "drmModeFreeEncoder", &mode_free_encoder);
    GetFuncAddress(lib, "drmModeGetCrtc", &mode_get_crtc);
    GetFuncAddress(lib, "drmModeSetCrtc", &mode_set_crtc);
    GetFuncAddress(lib, "drmModeFreeCrtc", &mode_free_crtc);
    GetFuncAddress(lib, "drmModeGetFB2", &mode_get_fb2);
    GetFuncAddress(lib, "drmModeAddFB2", &mode_add_fb2);
    GetFuncAddress(lib, "drmModeAddFB2WithModifiers",
                   &mode_add_fb2_with_modifiers);
    GetFuncAddress(lib, "drmModeFreeFB2", &mode_free_fb2);
    GetFuncAddress(lib, "drmModeAtomicAlloc", &mode_atomic_alloc);
    GetFuncAddress(lib, "drmModeAtomicFree", &mode_atomic_free);
    GetFuncAddress(lib, "drmModeAtomicCommit", &mode_atomic_commit);
    GetFuncAddress(lib, "drmModeAtomicAddProperty", &mode_atomic_add_property);
    GetFuncAddress(lib, "drmModeAtomicGetCursor", &mode_atomic_get_cursor);
    GetFuncAddress(lib, "drmModeAtomicSetCursor", &mode_atomic_set_cursor);
    GetFuncAddress(lib, "drmModeGetProperty", &mode_get_property);
    GetFuncAddress(lib, "drmModeFreeProperty", &mode_free_property);
    GetFuncAddress(lib, "drmModeGetPropertyBlob", &mode_get_property_blob);
    GetFuncAddress(lib, "drmModeFreePropertyBlob", &mode_free_property_blob);
    GetFuncAddress(lib, "drmModeFreeObjectProperties",
                   &mode_free_object_properties);
    GetFuncAddress(lib, "drmModeCreatePropertyBlob",
                   &mode_create_property_blob);
    GetFuncAddress(lib, "drmModeObjectGetProperties",
                   &mode_object_get_properties);
    GetFuncAddress(lib, "drmModeGetResources", &mode_get_resources);
    GetFuncAddress(lib, "drmModeFreeResources", &mode_free_resources);
    GetFuncAddress(lib, "drmModeGetPlaneResources", &mode_get_plane_resources);
    GetFuncAddress(lib, "drmModeFreePlaneResources",
                   &mode_free_plane_resources);
    GetFuncAddress(lib, "drmModeGetPlane", &mode_get_plane);
    GetFuncAddress(lib, "drmModeFreePlane", &mode_free_plane);
    GetFuncAddress(lib, "drmModeRmFB", &mode_rm_fb);
  }
}

LibDrmExports* LibDrm::operator->() const {
  return loadExports(nullptr);
}

LibDrmExports* LibDrm::loadExports(const char* library_path = nullptr) {
  static LibDrmExports exports = [&] {
    void* lib = dlopen(library_path ? library_path : "libdrm.so",
                       RTLD_LAZY | RTLD_LOCAL);
    return LibDrmExports(lib);
  }();

  return exports.mode_atomic_commit ? &exports : nullptr;
}

class LibDrm LibDrm;
