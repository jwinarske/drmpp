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
    GetFuncAddress(lib, "drmIoctl", &Ioctl);
    GetFuncAddress(lib, "drmSetClientCap", &SetClientCap);
    GetFuncAddress(lib, "drmGetDevice2", &GetDevice2);
    GetFuncAddress(lib, "drmGetDevices2", &GetDevices2);
    GetFuncAddress(lib, "drmFreeDevices", &FreeDevices);
    GetFuncAddress(lib, "drmGetMagic", &GetMagic);
    GetFuncAddress(lib, "drmAuthMagic", &AuthMagic);
    GetFuncAddress(lib, "drmGetVersion", &GetVersion);
    GetFuncAddress(lib, "drmFreeVersion", &FreeVersion);
    GetFuncAddress(lib, "drmGetCap", &GetCap);
    GetFuncAddress(lib, "drmGetDevice", &GetDevice);
    GetFuncAddress(lib, "drmFreeDevice", &FreeDevice);
    GetFuncAddress(lib, "drmModeGetConnectorCurrent", &ModeGetConnectorCurrent);
    GetFuncAddress(lib, "drmModeGetConnector", &ModeGetConnector);
    GetFuncAddress(lib, "drmModeFreeConnector", &ModeFreeConnector);
    GetFuncAddress(lib, "drmModeGetEncoder", &ModeGetEncoder);
    GetFuncAddress(lib, "drmModeFreeEncoder", &ModeFreeEncoder);
    GetFuncAddress(lib, "drmModeGetCrtc", &ModeGetCrtc);
    GetFuncAddress(lib, "drmModeSetCrtc", &ModeSetCrtc);
    GetFuncAddress(lib, "drmModeFreeCrtc", &ModeFreeCrtc);
    GetFuncAddress(lib, "drmModeGetFB", &ModeGetFB);
    GetFuncAddress(lib, "drmModeGetFB2", &ModeGetFB2);
    GetFuncAddress(lib, "drmModeAddFB", &ModeAddFB);
    GetFuncAddress(lib, "drmModeAddFB2", &ModeAddFB2);
    GetFuncAddress(lib, "drmModeAddFB2WithModifiers", &ModeAddFB2WithModifiers);
    GetFuncAddress(lib, "drmModeFreeFB", &ModeFreeFB);
    GetFuncAddress(lib, "drmModeFreeFB2", &ModeFreeFB2);
    GetFuncAddress(lib, "drmModeAtomicAlloc", &ModeAtomicAlloc);
    GetFuncAddress(lib, "drmModeAtomicFree", &ModeAtomicFree);
    GetFuncAddress(lib, "drmModeAtomicCommit", &ModeAtomicCommit);
    GetFuncAddress(lib, "drmModeAtomicAddProperty", &ModeAtomicAddProperty);
    GetFuncAddress(lib, "drmModeAtomicGetCursor", &ModeAtomicGetCursor);
    GetFuncAddress(lib, "drmModeAtomicSetCursor", &ModeAtomicSetCursor);
    GetFuncAddress(lib, "drmModeGetProperty", &ModeGetProperty);
    GetFuncAddress(lib, "drmModeFreeProperty", &ModeFreeProperty);
    GetFuncAddress(lib, "drmModeGetPropertyBlob", &ModeGetPropertyBlob);
    GetFuncAddress(lib, "drmModeFreePropertyBlob", &ModeFreePropertyBlob);
    GetFuncAddress(lib, "drmModeFreeObjectProperties",
                   &ModeFreeObjectProperties);
    GetFuncAddress(lib, "drmModeCreatePropertyBlob", &ModeCreatePropertyBlob);
    GetFuncAddress(lib, "drmModeObjectGetProperties", &ModeObjectGetProperties);
    GetFuncAddress(lib, "drmModeGetResources", &ModeGetResources);
    GetFuncAddress(lib, "drmModeFreeResources", &ModeFreeResources);
    GetFuncAddress(lib, "drmModeGetPlaneResources", &ModeGetPlaneResources);
    GetFuncAddress(lib, "drmModeFreePlaneResources", &ModeFreePlaneResources);
    GetFuncAddress(lib, "drmModeGetPlane", &ModeGetPlane);
    GetFuncAddress(lib, "drmModeFreePlane", &ModeFreePlane);
    GetFuncAddress(lib, "drmModeRmFB", &ModeRmFB);
  }
}

LibDrmExports* drm::operator->() const {
  return loadExports(nullptr);
}

LibDrmExports* drm::loadExports(const char* library_path = nullptr) {
  static LibDrmExports exports = [&] {
    void* lib = dlopen(library_path ? library_path : "libdrm.so",
                       RTLD_LAZY | RTLD_LOCAL);
    return LibDrmExports(lib);
  }();

  return exports.ModeAtomicCommit ? &exports : nullptr;
}

class drm drm;
