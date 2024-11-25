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

#ifndef INCLUDE_DRMPP_SHARED_LIBS_LIBDRM_H
#define INCLUDE_DRMPP_SHARED_LIBS_LIBDRM_H

#include <xf86drmMode.h>

struct LibDrmExports {
    LibDrmExports() = default;

    explicit LibDrmExports(void *lib);

    typedef int (*DrmIoctl)(int fd, unsigned long request, void *arg);

    typedef drmModeConnectorPtr (*DrmModeGetConnector)(int fd,
                                                       uint32_t connectorId);

    typedef void (*DrmModeFreeConnector)(drmModeConnectorPtr ptr);

    typedef drmModeEncoderPtr (*DrmModeGetEncoder)(int fd, uint32_t encoder_id);

    typedef void (*DrmModeFreeEncoder)(drmModeEncoderPtr ptr);

    typedef drmModeCrtcPtr (*DrmModeGetCrtc)(int fd, uint32_t crtcId);

    typedef int (*DrmModeSetCrtc)(int fd,
                                  uint32_t crtcId,
                                  uint32_t bufferId,
                                  uint32_t x,
                                  uint32_t y,
                                  uint32_t *connectors,
                                  int count,
                                  drmModeModeInfoPtr mode);

    typedef void (*DrmModeFreeCrtc)(drmModeCrtcPtr ptr);

    typedef drmModeFB2Ptr (*DrmModeGetFB2)(int fd, uint32_t bufferId);

    typedef int (*DrmModeAddFB2)(int fd,
                                 uint32_t width,
                                 uint32_t height,
                                 uint32_t pixel_format,
                                 const uint32_t bo_handles[4],
                                 const uint32_t pitches[4],
                                 const uint32_t offsets[4],
                                 uint32_t *buf_id,
                                 uint32_t flags);

    typedef int (*DrmModeAddFB2WithModifiers)(int fd,
                                              uint32_t width,
                                              uint32_t height,
                                              uint32_t pixel_format,
                                              const uint32_t bo_handles[4],
                                              const uint32_t pitches[4],
                                              const uint32_t offsets[4],
                                              const uint64_t modifier[4],
                                              uint32_t *buf_id,
                                              uint32_t flags);

    typedef void (*DrmModeFreeFB2)(drmModeFB2Ptr ptr);

    typedef drmModeAtomicReqPtr (*DrmModeAtomicAlloc)();

    typedef void (*DrmModeAtomicFree)(drmModeAtomicReqPtr req);

    typedef int (*DrmModeAtomicCommit)(int fd,
                                       drmModeAtomicReqPtr req,
                                       uint32_t flags,
                                       void *user_data);

    typedef int (*DrmModeAtomicAddProperty)(drmModeAtomicReqPtr req,
                                            uint32_t object_id,
                                            uint32_t property_id,
                                            uint64_t value);

    typedef int (*DrmModeAtomicGetCursor)(drmModeAtomicReqPtr req);

    typedef void (*DrmModeAtomicSetCursor)(drmModeAtomicReqPtr req, int cursor);

    typedef drmModePropertyPtr (*DrmModeGetProperty)(int fd, uint32_t propertyId);

    typedef void (*DrmModeFreeProperty)(drmModePropertyPtr ptr);

    typedef drmModePropertyBlobPtr (*DrmModeGetPropertyBlob)(int fd,
                                                             uint32_t blob_id);

    typedef void (*DrmModeFreePropertyBlob)(drmModePropertyBlobPtr ptr);

    typedef void (*DrmModeFreeObjectProperties)(drmModeObjectPropertiesPtr ptr);

    typedef int (*DrmModeCreatePropertyBlob)(int fd,
                                             const void *data,
                                             size_t size,
                                             uint32_t *id);

    typedef drmModeObjectPropertiesPtr (*DrmModeObjectGetProperties)(
        int fd,
        uint32_t object_id,
        uint32_t object_type);

    typedef drmModeResPtr (*DrmModeGetResources)(int fd);

    typedef void (*DrmModeFreeResources)(drmModeResPtr ptr);

    typedef drmModePlaneResPtr (*DrmModeGetPlaneResources)(int fd);

    typedef void (*DrmModeFreePlaneResources)(drmModePlaneResPtr ptr);

    typedef drmModePlanePtr (*DrmModeGetPlane)(int fd, uint32_t plane_id);

    typedef void (*DrmModeFreePlane)(drmModePlanePtr ptr);

    typedef int (*DrmModeRmFB)(int fd, uint32_t bufferId);

    typedef int (*DrmModeAddFB)(int fd,
                                uint32_t width,
                                uint32_t height,
                                uint8_t depth,
                                uint8_t bpp,
                                uint32_t pitch,
                                uint32_t bo_handle,
                                uint32_t *buf_id);

    typedef int (*DrmSetClientCap)(int fd, uint64_t capability, uint64_t value);

    DrmIoctl Ioctl = nullptr;
    DrmSetClientCap SetClientCap = nullptr;

    DrmModeGetConnector ModeGetConnector = nullptr;
    DrmModeFreeConnector ModeFreeConnector = nullptr;
    DrmModeGetEncoder ModeGetEncoder = nullptr;
    DrmModeFreeEncoder ModeFreeEncoder = nullptr;
    DrmModeGetCrtc ModeGetCrtc = nullptr;
    DrmModeSetCrtc ModeSetCrtc = nullptr;
    DrmModeFreeCrtc ModeFreeCrtc = nullptr;
    DrmModeGetFB2 ModeGetFB2 = nullptr;
    DrmModeAddFB ModeAddFB = nullptr;
    DrmModeAddFB2 ModeAddFB2 = nullptr;
    DrmModeAddFB2WithModifiers ModeAddFB2WithModifiers = nullptr;
    DrmModeFreeFB2 ModeFreeFB2 = nullptr;
    DrmModeAtomicAlloc ModeAtomicAlloc = nullptr;
    DrmModeAtomicFree ModeAtomicFree = nullptr;
    DrmModeAtomicCommit ModeAtomicCommit = nullptr;
    DrmModeAtomicAddProperty ModeAtomicAddProperty = nullptr;
    DrmModeAtomicGetCursor ModeAtomicGetCursor = nullptr;
    DrmModeAtomicSetCursor ModeAtomicSetCursor = nullptr;
    DrmModeGetProperty ModeGetProperty = nullptr;
    DrmModeFreeProperty ModeFreeProperty = nullptr;
    DrmModeGetPropertyBlob ModeGetPropertyBlob = nullptr;
    DrmModeFreePropertyBlob ModeFreePropertyBlob = nullptr;
    DrmModeFreeObjectProperties ModeFreeObjectProperties = nullptr;
    DrmModeCreatePropertyBlob ModeCreatePropertyBlob = nullptr;
    DrmModeObjectGetProperties ModeObjectGetProperties = nullptr;
    DrmModeGetResources ModeGetResources = nullptr;
    DrmModeFreeResources ModeFreeResources = nullptr;
    DrmModeGetPlaneResources ModeGetPlaneResources = nullptr;
    DrmModeFreePlaneResources ModeFreePlaneResources = nullptr;
    DrmModeGetPlane ModeGetPlane = nullptr;
    DrmModeFreePlane ModeFreePlane = nullptr;
    DrmModeRmFB ModeRmFB = nullptr;
};

class drm {
public:
    static bool IsPresent(const char *library_path = nullptr) {
        return loadExports(library_path) != nullptr;
    }

    LibDrmExports *operator->() const;

private:
    static LibDrmExports *loadExports(const char *library_path);
};

extern drm drm;

#endif  // INCLUDE_DRMPP_SHARED_LIBS_LIBDRM_H
