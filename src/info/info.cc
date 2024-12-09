/*
 * Copyright (c) 2024 The drmpp Contributors
 * Copyright (c) 2018 Scott Anderson
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 *
 */

#include "info/info.h"

#include <fstream>
#include <iostream>
#include <map>
#include <string>

#include <fcntl.h>
#include <sys/utsname.h>
#include <unistd.h>
#include <cstdlib>
#include <cstring>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include <rapidjson/stringbuffer.h>
#include <rapidjson/writer.h>

#include "logging/logging.h"
#include "shared_libs/libdrm.h"
#include "utils/utils.h"

namespace drmpp::info {

// according to CTA 861.G
enum { HDMI_STATIC_METADATA_TYPE1 = 0 };

unsigned long DrmInfo::tainted_info() {
  std::fstream file;
  file.open("/proc/sys/kernel/tainted", std::ios::in);

  std::string data;
  if (file.is_open()) {
    std::getline(file, data);
    file.close();
  } else {
    LOG_ERROR("Failed to open /proc/sys/kernel/tainted");
    return -1;
  }

  unsigned long tainted;
  try {
    tainted = std::strtoull(data.c_str(), nullptr, 10);
  } catch (std::invalid_argument& e) {
    LOG_ERROR("stdroull: {}", e.what());
    return {};
  } catch (std::out_of_range& e) {
    LOG_ERROR("stdroull: {}", e.what());
    return {};
  }

  return tainted;
}

rapidjson::Value DrmInfo::kernel_info(
    rapidjson::MemoryPoolAllocator<>& allocator) {
  utsname utsname{};
  if (uname(&utsname) != 0) {
    LOG_ERROR("[drmpp] uname failed to fetch system information.");
    return rapidjson::Value(
        rapidjson::kObjectType);  // Returning empty object with kObjectType
                                  // instead of default constructor
  }
  rapidjson::Value obj(rapidjson::kObjectType);
  obj.AddMember("sysname",
                rapidjson::Value(utsname.sysname,
                                 static_cast<rapidjson::SizeType>(
                                     std::strlen(utsname.sysname)),
                                 allocator)
                    .Move(),
                allocator);
  obj.AddMember("release",
                rapidjson::Value(utsname.release,
                                 static_cast<rapidjson::SizeType>(
                                     std::strlen(utsname.release)),
                                 allocator)
                    .Move(),
                allocator);
  obj.AddMember("version",
                rapidjson::Value(utsname.version,
                                 static_cast<rapidjson::SizeType>(
                                     std::strlen(utsname.version)),
                                 allocator)
                    .Move(),
                allocator);
  if (tainted_info()) {
    obj.AddMember("tainted", true, allocator);
  }
  return obj;
}

rapidjson::Value DrmInfo::driver_info(
    const int fd,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  std::map<const char*, uint64_t> client_caps = {
      {"STEREO_3D", DRM_CLIENT_CAP_STEREO_3D},                // kernel 3.4
      {"UNIVERSAL_PLANES", DRM_CLIENT_CAP_UNIVERSAL_PLANES},  // kernel 3.15
#ifdef DRM_CLIENT_CAP_ATOMIC
      {"ATOMIC", DRM_CLIENT_CAP_ATOMIC},  // kernel 4.2
#endif
#ifdef DRM_CLIENT_CAP_ASPECT_RATIO
      {"ASPECT_RATIO", DRM_CLIENT_CAP_ASPECT_RATIO},  // kernel 4.9
#endif
      {"WRITEBACK_CONNECTORS",
       DRM_CLIENT_CAP_WRITEBACK_CONNECTORS},  // kernel 4.14
#ifdef DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT
      {"CURSOR_PLANE_HOTSPOT",
       DRM_CLIENT_CAP_CURSOR_PLANE_HOTSPOT},  // kernel 5.13
#endif
  };

  // A map to store the general capabilities (name to DRM_CAP constant)
  std::map<const char*, uint64_t> caps = {
      {"DUMB_BUFFER", DRM_CAP_DUMB_BUFFER},                    // kernel 3.1
      {"PRIME", DRM_CAP_PRIME},                                // kernel 3.3
      {"DUMB_PREFERRED_DEPTH", DRM_CAP_DUMB_PREFERRED_DEPTH},  // kernel 3.8
      {"TIMESTAMP_MONOTONIC", DRM_CAP_TIMESTAMP_MONOTONIC},    // kernel 3.8
      {"ASYNC_PAGE_FLIP", DRM_CAP_ASYNC_PAGE_FLIP},            // kernel 3.8
      {"DUMB_PREFER_SHADOW", DRM_CAP_DUMB_PREFER_SHADOW},      // kernel 3.9
      {"VBLANK_HIGH_CRTC", DRM_CAP_VBLANK_HIGH_CRTC},          // kernel 3.10
      {"CURSOR_WIDTH", DRM_CAP_CURSOR_WIDTH},                  // kernel 3.10
      {"CURSOR_HEIGHT", DRM_CAP_CURSOR_HEIGHT},                // kernel 3.10
#ifdef DRM_CAP_PAGE_FLIP_TARGET
      {"PAGE_FLIP_TARGET", DRM_CAP_PAGE_FLIP_TARGET},  // kernel 4.8
#endif
#ifdef DRM_CAP_CRTC_IN_VBLANK_EVENT
      {"CRTC_IN_VBLANK_EVENT", DRM_CAP_CRTC_IN_VBLANK_EVENT},  // kernel 4.12
#endif
#ifdef DRM_CAP_ADDFB2_MODIFIERS
      {"ADDFB2_MODIFIERS", DRM_CAP_ADDFB2_MODIFIERS},  // kernel 4.15
#endif
#ifdef DRM_CAP_SYNCOBJ
      {"SYNCOBJ", DRM_CAP_SYNCOBJ},  // kernel 4.15
#endif
#ifdef DRM_CAP_SYNCOBJ_TIMELINE
      {"SYNCOBJ_TIMELINE", DRM_CAP_SYNCOBJ_TIMELINE},  // kernel 5.0
#endif
#ifdef DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP
      {"ATOMIC_ASYNC_PAGE_FLIP", DRM_CAP_ATOMIC_ASYNC_PAGE_FLIP},  // kernel 5.0
#endif
  };

  drmVersion* ver = drm->GetVersion(fd);
  if (!ver) {
    LOG_ERROR("[drmpp] drmGetVersion");
    return {};
  }

  rapidjson::Value obj(rapidjson::kObjectType);
  obj.AddMember(
      "name",
      rapidjson::Value(ver->name, std::strlen(ver->name), allocator).Move(),
      allocator);
  obj.AddMember(
      "desc",
      rapidjson::Value(ver->desc, std::strlen(ver->desc), allocator).Move(),
      allocator);

  rapidjson::Value ver_obj(rapidjson::kObjectType);
  ver_obj.AddMember("major", ver->version_major, allocator);
  ver_obj.AddMember("minor", ver->version_minor, allocator);
  ver_obj.AddMember("patch", ver->version_patchlevel, allocator);
  ver_obj.AddMember(
      "date",
      rapidjson::Value(ver->date, std::strlen(ver->date), allocator).Move(),
      allocator);
  obj.AddMember("version", ver_obj, allocator);

  drm->FreeVersion(ver);

  obj.AddMember("kernel", kernel_info(allocator), allocator);

  rapidjson::Value client_caps_obj(rapidjson::kObjectType);
  for (auto [name, cap] : client_caps) {
    bool supported = drm->SetClientCap(fd, cap, 1) == 0;
    client_caps_obj.AddMember(rapidjson::Value(name, allocator).Move(),
                              supported, allocator);
  }
  obj.AddMember("client_caps", client_caps_obj, allocator);

  rapidjson::Value caps_obj(rapidjson::kObjectType);
  for (auto [name, cap] : caps) {
    if (drm->GetCap(fd, cap, &cap) == 0) {
      rapidjson::Value cap_value;
      cap_value.SetUint64(cap);
      caps_obj.AddMember(rapidjson::Value(name, allocator).Move(), cap_value,
                         allocator);
    }
  }
  obj.AddMember("caps", caps_obj, allocator);

  return obj;
}

rapidjson::Value DrmInfo::device_info(
    const int fd,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  drmDevice* dev;
  if (drm->GetDevice(fd, &dev) != 0) {
    LOG_ERROR("[drmpp] drmGetDevice");
    return {};
  }

  rapidjson::Value obj(rapidjson::kObjectType);
  obj.AddMember("available_nodes", dev->available_nodes, allocator);
  obj.AddMember("bus_type", dev->bustype, allocator);

  rapidjson::Value device_data_obj(rapidjson::kObjectType);
  rapidjson::Value bus_data_obj(rapidjson::kObjectType);
  rapidjson::Value compatible_arr(rapidjson::kArrayType);

  switch (dev->bustype) {
    case DRM_BUS_PCI: {
      drmPciDeviceInfo* pci_dev = dev->deviceinfo.pci;
      drmPciBusInfo* pci_bus = dev->businfo.pci;

      device_data_obj.AddMember(
          "vendor", rapidjson::Value().SetUint64(pci_dev->vendor_id),
          allocator);
      device_data_obj.AddMember(
          "device", rapidjson::Value().SetUint64(pci_dev->device_id),
          allocator);
      device_data_obj.AddMember(
          "subsystem_vendor",
          rapidjson::Value().SetUint64(pci_dev->subvendor_id), allocator);
      device_data_obj.AddMember(
          "subsystem_device",
          rapidjson::Value().SetUint64(pci_dev->subdevice_id), allocator);

      bus_data_obj.AddMember(
          "domain", rapidjson::Value().SetUint64(pci_bus->domain), allocator);
      bus_data_obj.AddMember("bus", rapidjson::Value().SetUint64(pci_bus->bus),
                             allocator);
      bus_data_obj.AddMember("slot", rapidjson::Value().SetUint64(pci_bus->dev),
                             allocator);
      bus_data_obj.AddMember(
          "function", rapidjson::Value().SetUint64(pci_bus->func), allocator);
      break;
    }
    case DRM_BUS_USB: {
      drmUsbDeviceInfo* usb_dev = dev->deviceinfo.usb;
      drmUsbBusInfo* usb_bus = dev->businfo.usb;

      device_data_obj.AddMember(
          "vendor", rapidjson::Value().SetUint64(usb_dev->vendor), allocator);
      device_data_obj.AddMember(
          "product", rapidjson::Value().SetUint64(usb_dev->product), allocator);
      bus_data_obj.AddMember("bus", rapidjson::Value().SetUint64(usb_bus->bus),
                             allocator);
      bus_data_obj.AddMember(
          "device", rapidjson::Value().SetUint64(usb_bus->dev), allocator);
      break;
    }
    case DRM_BUS_PLATFORM: {
      drmPlatformDeviceInfo* platform_dev = dev->deviceinfo.platform;
      drmPlatformBusInfo* platform_bus = dev->businfo.platform;
      for (size_t i = 0; platform_dev->compatible[i]; ++i) {
        LOG_INFO("compatible: {}", platform_dev->compatible[i]);
        compatible_arr
            .PushBack(rapidjson::Value()
                          .SetString(platform_dev->compatible[i],
                                     std::strlen(platform_dev->compatible[i]))
                          .Move(),
                      allocator)
            .Move();
      }
      device_data_obj.AddMember("compatible", compatible_arr, allocator);

      bus_data_obj.AddMember(
          "fullname",
          rapidjson::Value(platform_bus->fullname,
                           strlen(platform_bus->fullname), allocator)
              .Move(),
          allocator);
      break;
    }
    case DRM_BUS_HOST1X: {
      drmHost1xDeviceInfo* host1x_dev = dev->deviceinfo.host1x;
      drmHost1xBusInfo* host1x_bus = dev->businfo.host1x;

      for (size_t i = 0; host1x_dev->compatible[i]; ++i) {
        compatible_arr.PushBack(
            rapidjson::Value()
                .SetString(host1x_dev->compatible[i],
                           std::strlen(host1x_dev->compatible[i]))
                .Move(),
            allocator);
      }
      device_data_obj.AddMember("compatible", compatible_arr, allocator);

      bus_data_obj.AddMember(
          "fullname",
          rapidjson::Value(host1x_bus->fullname,
                           std::strlen(host1x_bus->fullname), allocator)
              .Move(),
          allocator);
      break;
    }
    default:
      break;
  }
  obj.AddMember("device_data", device_data_obj, allocator);
  obj.AddMember("bus_data", bus_data_obj, allocator);

  drm->FreeDevice(&dev);

  return obj;
}

rapidjson::Value DrmInfo::in_formats_info(
    const int fd,
    const uint32_t blob_id,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value arr(rapidjson::kArrayType);

  drmModePropertyBlobRes* blob = drm->ModeGetPropertyBlob(fd, blob_id);
  if (!blob) {
    LOG_ERROR("[drmpp] drmModeGetPropertyBlob");
    return {};
  }

  const auto data = static_cast<drm_format_modifier_blob*>(blob->data);

  const auto fmts = reinterpret_cast<uint32_t*>(reinterpret_cast<char*>(data) +
                                                data->formats_offset);

  auto* mods = reinterpret_cast<struct drm_format_modifier*>(
      reinterpret_cast<char*>(data) + data->modifiers_offset);

  for (uint32_t i = 0; i < data->count_modifiers; ++i) {
    rapidjson::Value mod_obj(rapidjson::kObjectType);
    mod_obj.AddMember(
        "modifier", rapidjson::Value().SetUint64(mods[i].modifier), allocator);

    rapidjson::Value fmts_arr(rapidjson::kArrayType);
    for (uint64_t j = 0; j < 64; ++j) {
      if (mods[i].formats & (1ull << j)) {
        uint32_t fmt = fmts[j + mods[i].offset];
        fmts_arr.PushBack(fmt, allocator);
      }
    }
    mod_obj.AddMember("formats", fmts_arr, allocator);

    arr.PushBack(mod_obj, allocator);
  }

  drm->ModeFreePropertyBlob(blob);

  return arr;
}

rapidjson::Value DrmInfo::mode_info(
    const drmModeModeInfo* mode,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value obj(rapidjson::kObjectType);

  obj.AddMember("clock", rapidjson::Value().SetUint64(mode->clock), allocator);

  obj.AddMember("hdisplay", rapidjson::Value().SetUint64(mode->hdisplay),
                allocator);
  obj.AddMember("hsync_start", rapidjson::Value().SetUint64(mode->hsync_start),
                allocator);
  obj.AddMember("hsync_end", rapidjson::Value().SetUint64(mode->hsync_end),
                allocator);
  obj.AddMember("htotal", rapidjson::Value().SetUint64(mode->htotal),
                allocator);
  obj.AddMember("hskew", rapidjson::Value().SetUint64(mode->hskew), allocator);

  obj.AddMember("vdisplay", rapidjson::Value().SetUint64(mode->vdisplay),
                allocator);
  obj.AddMember("vsync_start", rapidjson::Value().SetUint64(mode->vsync_start),
                allocator);
  obj.AddMember("vsync_end", rapidjson::Value().SetUint64(mode->vsync_end),
                allocator);
  obj.AddMember("vtotal", rapidjson::Value().SetUint64(mode->vtotal),
                allocator);
  obj.AddMember("vscan", rapidjson::Value().SetUint64(mode->vscan), allocator);

  obj.AddMember("vrefresh", rapidjson::Value().SetUint64(mode->vrefresh),
                allocator);
  obj.AddMember("flags", rapidjson::Value().SetUint64(mode->flags), allocator);
  obj.AddMember("type", rapidjson::Value().SetUint64(mode->type), allocator);
  obj.AddMember(
      "name",
      rapidjson::Value(mode->name, std::strlen(mode->name), allocator).Move(),
      allocator);
  return obj;
}

rapidjson::Value DrmInfo::mode_id_info(
    const int fd,
    const uint32_t blob_id,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  drmModePropertyBlobRes* blob = drm->ModeGetPropertyBlob(fd, blob_id);
  if (!blob) {
    LOG_ERROR("[drmpp] drmModeGetPropertyBlob");
    return {};
  }

  const auto mode = static_cast<drmModeModeInfo*>(blob->data);

  auto obj = mode_info(mode, allocator);

  drm->ModeFreePropertyBlob(blob);

  return obj;
}

rapidjson::Value DrmInfo::writeback_pixel_formats_info(
    const int fd,
    const uint32_t blob_id,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value arr(rapidjson::kArrayType);

  drmModePropertyBlobRes* blob = drm->ModeGetPropertyBlob(fd, blob_id);
  if (!blob) {
    LOG_ERROR("[drmpp] drmModeGetPropertyBlob");
    return {};
  }

  const auto fmts = static_cast<uint32_t*>(blob->data);
  const uint32_t fmts_len = blob->length / sizeof(fmts[0]);
  for (uint32_t i = 0; i < fmts_len; ++i) {
    arr.PushBack(fmts[i], allocator);
  }

  drm->ModeFreePropertyBlob(blob);

  return arr;
}

rapidjson::Value DrmInfo::path_info(const int fd, const uint32_t blob_id) {
  drmModePropertyBlobRes* blob = drm->ModeGetPropertyBlob(fd, blob_id);
  if (!blob) {
    LOG_ERROR("[drmpp] drmModeGetPropertyBlob");
    return {};
  }

  rapidjson::Value obj;

  if (blob->length) {
    obj.SetString(static_cast<char*>(blob->data), blob->length);
  }

  drm->ModeFreePropertyBlob(blob);

  return obj;
}

rapidjson::Value DrmInfo::hdr_output_metadata_info(
    const int fd,
    const uint32_t blob_id,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  drmModePropertyBlobRes* blob = drm->ModeGetPropertyBlob(fd, blob_id);
  if (!blob) {
    LOG_ERROR("[drmpp] drmModeGetPropertyBlob");
    return {};
  }

  rapidjson::Value obj(rapidjson::kObjectType);
  do {
    // The type field in the struct comes first and is an u32
    if (blob->length < sizeof(uint32_t)) {
      LOG_ERROR("[drmpp] HDR output metadata blob too short");
      break;
    }

    const auto meta = static_cast<hdr_output_metadata*>(blob->data);

    obj.AddMember("type", meta->metadata_type, allocator);

    if (meta->metadata_type == HDMI_STATIC_METADATA_TYPE1) {
      constexpr size_t min_size =
          offsetof(struct hdr_output_metadata, hdmi_metadata_type1) +
          sizeof(struct hdr_metadata_infoframe);
      if (blob->length < min_size) {
        LOG_ERROR("[drmpp] HDR output metadata blob too short\n");
        break;
      }

      const hdr_metadata_infoframe* info = &meta->hdmi_metadata_type1;
      obj.AddMember("eotf", info->eotf, allocator);
      // TODO: maybe add info->metadata_type, but seems to be the same as
      // meta->metadata_type?
      rapidjson::Value dp_obj(rapidjson::kObjectType);
      static const char* dp_keys[] = {"r", "g", "b"};
      for (size_t i = 0; i < 3; i++) {
        rapidjson::Value coord_obj(rapidjson::kObjectType);
        coord_obj.AddMember("x",
                            rapidjson::Value().SetDouble(
                                info->display_primaries[i].x / 50000.0),
                            allocator);
        coord_obj.AddMember("y",
                            rapidjson::Value().SetDouble(
                                info->display_primaries[i].y / 50000.0),
                            allocator);
        dp_obj.AddMember(rapidjson::Value(dp_keys[i], allocator).Move(),
                         coord_obj, allocator);
      }
      obj.AddMember("display_primaries", dp_obj, allocator);
      rapidjson::Value coord_obj(rapidjson::kObjectType);
      coord_obj.AddMember(
          "x", rapidjson::Value().SetDouble(info->white_point.x / 50000.0),
          allocator);
      coord_obj.AddMember(
          "y", rapidjson::Value().SetDouble(info->white_point.y / 50000.0),
          allocator);
      obj.AddMember("white_point", coord_obj, allocator);
      obj.AddMember(
          "max_display_mastering_luminance",
          rapidjson::Value().SetInt(info->max_display_mastering_luminance),
          allocator);
      obj.AddMember("min_display_mastering_luminance",
                    rapidjson::Value().SetDouble(
                        info->min_display_mastering_luminance / 10000.0),
                    allocator);
      obj.AddMember("max_cll", rapidjson::Value().SetInt(info->max_cll),
                    allocator);
      obj.AddMember("max_fall", rapidjson::Value().SetInt(info->max_fall),
                    allocator);
    }
  } while (false);

  drm->ModeFreePropertyBlob(blob);
  return obj;
}

rapidjson::Value DrmInfo::fb2_info(
    const int fd,
    const uint32_t id,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  if (!drm->ModeGetFB2) {
    return {};
  }
  drmModeFB2* fb2 = drm->ModeGetFB2(fd, id);
  if (!fb2 && errno != EINVAL) {
    LOG_ERROR("drmModeGetFB2");
    return {};
  }
  if (fb2) {
    rapidjson::Value obj(rapidjson::kObjectType);
    obj.AddMember("id", rapidjson::Value().SetUint64(fb2->fb_id), allocator);
    obj.AddMember("width", rapidjson::Value().SetUint64(fb2->width), allocator);
    obj.AddMember("height", rapidjson::Value().SetUint64(fb2->height),
                  allocator);

    obj.AddMember("format", rapidjson::Value().SetUint64(fb2->pixel_format),
                  allocator);
    if (fb2->flags & DRM_MODE_FB_MODIFIERS) {
      obj.AddMember("modifier", rapidjson::Value().SetUint64(fb2->modifier),
                    allocator);
    }

    rapidjson::Value planes_arr(rapidjson::kArrayType);

    for (size_t i = 0; i < std::size(fb2->pitches); i++) {
      if (!fb2->pitches[i])
        continue;

      rapidjson::Value plane_obj(rapidjson::kObjectType);
      plane_obj.AddMember(
          "offset", rapidjson::Value().SetUint64(fb2->offsets[i]), allocator);
      plane_obj.AddMember(
          "pitch", rapidjson::Value().SetUint64(fb2->pitches[i]), allocator);

      planes_arr.PushBack(plane_obj.Move(), allocator);
    }

    obj.AddMember("planes", planes_arr, allocator);

    drm->ModeFreeFB2(fb2);

    return obj;
  }
  return {};
}

rapidjson::Value DrmInfo::fb_info(const int fd,
                                  const uint32_t id,
                                  rapidjson::MemoryPoolAllocator<>& allocator) {
  drmModeFB* fb = drm->ModeGetFB(fd, id);
  if (!fb) {
    LOG_ERROR("[drmpp] drmModeGetFB");
    return {};
  }

  rapidjson::Value obj(rapidjson::kObjectType);
  obj.AddMember("id", rapidjson::Value().SetUint64(fb->fb_id), allocator);
  obj.AddMember("width", rapidjson::Value().SetUint64(fb->width), allocator);
  obj.AddMember("height", rapidjson::Value().SetUint64(fb->height), allocator);

  // Legacy properties
  obj.AddMember("pitch", rapidjson::Value().SetUint64(fb->pitch), allocator);
  obj.AddMember("bpp", rapidjson::Value().SetUint64(fb->bpp), allocator);
  obj.AddMember("depth", rapidjson::Value().SetUint64(fb->depth), allocator);

  drm->ModeFreeFB(fb);

  return obj;
}

rapidjson::Value DrmInfo::properties_info(
    const int fd,
    const uint32_t id,
    const uint32_t type,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  drmModeObjectProperties* props = drm->ModeObjectGetProperties(fd, id, type);
  if (!props) {
    LOG_ERROR("[drmpp] drmModeObjectGetProperties");
    return {};
  }

  rapidjson::Value obj(rapidjson::kObjectType);

  for (uint32_t i = 0; i < props->count_props; ++i) {
    drmModePropertyRes* prop = drm->ModeGetProperty(fd, props->props[i]);
    if (!prop) {
      LOG_ERROR("[drmpp] drmModeGetProperty");
      continue;
    }

    uint32_t flags = prop->flags;
    uint32_t t =
        flags & (DRM_MODE_PROP_LEGACY_TYPE | DRM_MODE_PROP_EXTENDED_TYPE);
    bool atomic = flags & DRM_MODE_PROP_ATOMIC;
    bool immutable = flags & DRM_MODE_PROP_IMMUTABLE;
    uint64_t value = props->prop_values[i];

    rapidjson::Value prop_obj(rapidjson::kObjectType);
    prop_obj.AddMember("id", rapidjson::Value().SetUint64(prop->prop_id),
                       allocator);
    prop_obj.AddMember("flags", rapidjson::Value().SetUint64(flags), allocator);
    prop_obj.AddMember("type", rapidjson::Value().SetUint64(t), allocator);
    prop_obj.AddMember("atomic", rapidjson::Value().SetBool(atomic), allocator);
    prop_obj.AddMember("immutable", rapidjson::Value().SetBool(immutable),
                       allocator);
    prop_obj.AddMember("raw_value", rapidjson::Value().SetUint64(value),
                       allocator);

    rapidjson::Value spec_obj;
    switch (t) {
      case DRM_MODE_PROP_RANGE: {
        spec_obj = rapidjson::kObjectType;
        spec_obj.AddMember("min", rapidjson::Value().SetUint64(prop->values[0]),
                           allocator);
        spec_obj.AddMember("max", rapidjson::Value().SetUint64(prop->values[1]),
                           allocator);
        break;
      }
      case DRM_MODE_PROP_ENUM:
      case DRM_MODE_PROP_BITMASK: {
        spec_obj = rapidjson::kArrayType;
        for (int j = 0; j < prop->count_enums; ++j) {
          rapidjson::Value item_obj(rapidjson::kObjectType);
          item_obj.AddMember(
              "name",
              rapidjson::Value(prop->enums[j].name,
                               std::strlen(prop->enums[j].name), allocator)
                  .Move(),
              allocator);
          item_obj.AddMember("value",
                             rapidjson::Value().SetUint64(prop->enums[j].value),
                             allocator);
          spec_obj.PushBack(item_obj, allocator);
        }
        break;
      }
      case DRM_MODE_PROP_OBJECT: {
        spec_obj.SetUint64(prop->values[0]);
        break;
      }
      case DRM_MODE_PROP_SIGNED_RANGE: {
        spec_obj = rapidjson::kObjectType;
        spec_obj.AddMember(
            "min",
            rapidjson::Value().SetInt64(static_cast<int64_t>(prop->values[0])),
            allocator);
        spec_obj.AddMember(
            "max",
            rapidjson::Value().SetInt64(static_cast<int64_t>(prop->values[1])),
            allocator);
        break;
      }
      default:
        break;
    }
    prop_obj.AddMember("spec", spec_obj, allocator);

    rapidjson::Value value_obj;
    switch (t) {
      case DRM_MODE_PROP_RANGE:
      case DRM_MODE_PROP_ENUM:
      case DRM_MODE_PROP_BITMASK:
      case DRM_MODE_PROP_OBJECT: {
        value_obj.SetUint64(value);
        break;
      }
      case DRM_MODE_PROP_BLOB: {
        // TODO: base64-encode blob contents
        value_obj = rapidjson::Value().SetNull();
        break;
      }
      case DRM_MODE_PROP_SIGNED_RANGE: {
        value_obj.SetInt64(static_cast<int64_t>(value));
        break;
      }
      default:
        break;
    }
    prop_obj.AddMember("value", value_obj, allocator);

    rapidjson::Value data_obj;
    switch (t) {
      case DRM_MODE_PROP_BLOB: {
        if (!value) {
          break;
        }
        if (strcmp(prop->name, "IN_FORMATS") == 0) {
          data_obj = in_formats_info(fd, value, allocator);
        } else if (strcmp(prop->name, "MODE_ID") == 0) {
          data_obj = mode_id_info(fd, value, allocator);
        } else if (strcmp(prop->name, "WRITEBACK_PIXEL_FORMATS") == 0) {
          data_obj = writeback_pixel_formats_info(fd, value, allocator);
        } else if (strcmp(prop->name, "PATH") == 0) {
          data_obj = path_info(fd, value);
        } else if (strcmp(prop->name, "HDR_OUTPUT_METADATA") == 0) {
          data_obj = hdr_output_metadata_info(fd, value, allocator);
        }
        break;
      }
      case DRM_MODE_PROP_RANGE: {
        // This is a special case, as the SRC_* properties are
        // in 16.16 fixed point
        if (strncmp(prop->name, "SRC_", 4) == 0) {
          data_obj.SetUint64(value >> 16);
        }
        break;
      }
      case DRM_MODE_PROP_OBJECT: {
        if (!value) {
          break;
        }
        if (strcmp(prop->name, "FB_ID") == 0) {
          data_obj = fb2_info(fd, value, allocator);
          if (data_obj.IsNull()) {
            data_obj = fb_info(fd, value, allocator);
          }
        }
        break;
      }
      default:
        break;
    }
    prop_obj.AddMember("data", data_obj, allocator);

    obj.AddMember(rapidjson::Value(prop->name, allocator).Move(), prop_obj,
                  allocator);

    drm->ModeFreeProperty(prop);
  }

  drm->ModeFreeObjectProperties(props);

  return obj;
}

rapidjson::Value DrmInfo::connectors_info(
    const int fd,
    const drmModeRes* res,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value arr(rapidjson::kArrayType);

  for (int i = 0; i < res->count_connectors; ++i) {
    drmModeConnector* conn =
        drm->ModeGetConnectorCurrent(fd, res->connectors[i]);
    if (!conn) {
      LOG_ERROR("[drmpp] drmModeGetConnectorCurrent");
      continue;
    }

    rapidjson::Value conn_obj(rapidjson::kObjectType);
    conn_obj.AddMember("id", rapidjson::Value().SetUint64(conn->connector_id),
                       allocator);
    conn_obj.AddMember(
        "type", rapidjson::Value().SetUint64(conn->connector_type), allocator);
    conn_obj.AddMember("status", rapidjson::Value().SetUint64(conn->connection),
                       allocator);
    conn_obj.AddMember("phy_width", rapidjson::Value().SetUint64(conn->mmWidth),
                       allocator);
    conn_obj.AddMember("phy_height",
                       rapidjson::Value().SetUint64(conn->mmHeight), allocator);
    conn_obj.AddMember("subpixel", rapidjson::Value().SetUint64(conn->subpixel),
                       allocator);
    conn_obj.AddMember("encoder_id",
                       rapidjson::Value().SetUint64(conn->encoder_id),
                       allocator);

    rapidjson::Value encoders_arr(rapidjson::kArrayType);
    for (int j = 0; j < conn->count_encoders; ++j) {
      encoders_arr.PushBack(rapidjson::Value().SetUint64(conn->encoders[j]),
                            allocator);
    }
    conn_obj.AddMember("encoders", encoders_arr, allocator);

    rapidjson::Value modes_arr(rapidjson::kArrayType);
    for (int j = 0; j < conn->count_modes; ++j) {
      const drmModeModeInfo* mode = &conn->modes[j];
      modes_arr.PushBack(mode_info(mode, allocator), allocator);
    }
    conn_obj.AddMember("modes", modes_arr, allocator);

    rapidjson::Value props_obj = properties_info(
        fd, conn->connector_id, DRM_MODE_OBJECT_CONNECTOR, allocator);
    conn_obj.AddMember("properties", props_obj, allocator);

    drm->ModeFreeConnector(conn);

    arr.PushBack(conn_obj, allocator);
  }

  return arr;
}

rapidjson::Value DrmInfo::encoders_info(
    const int fd,
    const drmModeRes* res,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value arr(rapidjson::kArrayType);

  for (int i = 0; i < res->count_encoders; ++i) {
    drmModeEncoder* enc = drm->ModeGetEncoder(fd, res->encoders[i]);
    if (!enc) {
      LOG_ERROR("[drmpp] drmModeGetEncoder");
      continue;
    }

    rapidjson::Value enc_obj(rapidjson::kObjectType);
    enc_obj.AddMember("id", rapidjson::Value().SetUint64(enc->encoder_id),
                      allocator);
    enc_obj.AddMember("type", rapidjson::Value().SetUint64(enc->encoder_type),
                      allocator);
    enc_obj.AddMember("crtc_id", rapidjson::Value().SetUint64(enc->crtc_id),
                      allocator);
    enc_obj.AddMember("possible_crtcs",
                      rapidjson::Value().SetUint64(enc->possible_crtcs),
                      allocator);
    enc_obj.AddMember("possible_clones",
                      rapidjson::Value().SetUint64(enc->possible_clones),
                      allocator);

    drm->ModeFreeEncoder(enc);

    arr.PushBack(enc_obj, allocator);
  }

  return arr;
}

rapidjson::Value DrmInfo::crtcs_info(
    const int fd,
    const drmModeRes* res,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  rapidjson::Value arr(rapidjson::kArrayType);

  for (int i = 0; i < res->count_crtcs; ++i) {
    drmModeCrtc* crtc = drm->ModeGetCrtc(fd, res->crtcs[i]);
    if (!crtc) {
      LOG_ERROR("[drmpp] drmModeGetCrtc");
      continue;
    }

    rapidjson::Value crtc_obj(rapidjson::kObjectType);
    crtc_obj.AddMember("id", rapidjson::Value().SetUint64(crtc->crtc_id),
                       allocator);
    crtc_obj.AddMember("fb_id", rapidjson::Value().SetUint64(crtc->buffer_id),
                       allocator);
    crtc_obj.AddMember("x", rapidjson::Value().SetUint64(crtc->x), allocator);
    crtc_obj.AddMember("y", rapidjson::Value().SetUint64(crtc->y), allocator);

    if (crtc->mode_valid) {
      crtc_obj.AddMember("mode", mode_info(&crtc->mode, allocator), allocator);
    } else {
      crtc_obj.AddMember("mode", {}, allocator);
    }
    crtc_obj.AddMember("gamma_size",
                       rapidjson::Value().SetInt(crtc->gamma_size), allocator);

    rapidjson::Value props_obj =
        properties_info(fd, crtc->crtc_id, DRM_MODE_OBJECT_CRTC, allocator);
    crtc_obj.AddMember("properties", props_obj, allocator);

    drm->ModeFreeCrtc(crtc);

    arr.PushBack(crtc_obj, allocator);
  }

  return arr;
}

rapidjson::Value DrmInfo::planes_info(
    int fd,
    rapidjson::MemoryPoolAllocator<>& allocator) {
  drmModePlaneRes* res = drm->ModeGetPlaneResources(fd);
  if (!res) {
    LOG_ERROR("[drmpp] drmModeGetPlaneResources");
    return {};
  }

  rapidjson::Value arr(rapidjson::kArrayType);

  for (uint32_t i = 0; i < res->count_planes; ++i) {
    drmModePlane* plane = drm->ModeGetPlane(fd, res->planes[i]);
    if (!plane) {
      LOG_ERROR("[drmpp] drmModeGetPlane");
      continue;
    }

    rapidjson::Value plane_obj(rapidjson::kObjectType);
    plane_obj.AddMember("id", rapidjson::Value().SetUint64(plane->plane_id),
                        allocator);
    plane_obj.AddMember("possible_crtcs",
                        rapidjson::Value().SetUint64(plane->possible_crtcs),
                        allocator);
    plane_obj.AddMember("crtc_id", rapidjson::Value().SetUint64(plane->crtc_id),
                        allocator);
    plane_obj.AddMember("fb_id", rapidjson::Value().SetUint64(plane->fb_id),
                        allocator);
    plane_obj.AddMember("crtc_x", rapidjson::Value().SetUint64(plane->crtc_x),
                        allocator);
    plane_obj.AddMember("crtc_y", rapidjson::Value().SetUint64(plane->crtc_y),
                        allocator);
    plane_obj.AddMember("x", rapidjson::Value().SetUint64(plane->x), allocator);
    plane_obj.AddMember("y", rapidjson::Value().SetUint64(plane->y), allocator);
    plane_obj.AddMember("gamma_size",
                        rapidjson::Value().SetUint64(plane->gamma_size),
                        allocator);

    rapidjson::Value val(rapidjson::kNullType);
    if (plane->fb_id) {
      val = fb2_info(fd, plane->fb_id, allocator);
      if (val.IsNull()) {
        val = fb_info(fd, plane->fb_id, allocator);
      }
    }
    plane_obj.AddMember("fb", val.Move(), allocator);

    rapidjson::Value formats_arr(rapidjson::kArrayType);
    for (uint32_t j = 0; j < plane->count_formats; ++j) {
      formats_arr.PushBack(rapidjson::Value().SetUint64(plane->formats[j]),
                           allocator);
    }
    plane_obj.AddMember("formats", formats_arr, allocator);

    rapidjson::Value props_obj =
        properties_info(fd, plane->plane_id, DRM_MODE_OBJECT_PLANE, allocator);
    plane_obj.AddMember("properties", props_obj, allocator);

    drm->ModeFreePlane(plane);  // Ensure plane objects are cleared

    arr.PushBack(plane_obj, allocator);
  }

  drm->ModeFreePlaneResources(res);  // Ensure resource objects are cleared

  return arr;  // Return the assembled array
}

std::string escape_path(const char* path) {
  std::string escaped_path;
  for (const char* p = path; *p; ++p) {
    switch (*p) {
      case '\\':
        escaped_path += "\\\\";
        break;
      case '"':
        escaped_path += "\\\"";
        break;
      case '/':
        escaped_path += "\\/";
        break;
      default:
        if (*p < 0x20 || *p > 0x7E) {
          std::ostringstream oss;
          oss << "\\u" << std::hex << std::setw(4) << std::setfill('0')
              << (int)(*p & 0xFF);
          escaped_path += oss.str();
        } else {
          escaped_path += *p;
        }
        break;
    }
  }
  return escaped_path;
}

void DrmInfo::node_info(const char* path, rapidjson::Document& d) {
  const int fd = open(path, O_RDONLY);
  if (fd < 0) {
    LOG_ERROR("[drmpp] Failed to open {}: {}", path,
              strerror(errno));  // Logging errno
    d.SetNull();
    return;
  }

  struct FdGuard {
    int fd;
    explicit FdGuard(const int fd) : fd(fd) {}
    ~FdGuard() { close(fd); }
  } fdGuard(fd);  // Ensure fd is closed when exiting scope

  auto& allocator = d.GetAllocator();

  std::string escaped_path = escape_path(path);
  rapidjson::Value key(escaped_path.c_str(), allocator);

  rapidjson::Value dev_node(rapidjson::kObjectType);
  dev_node.AddMember("driver", driver_info(fd, allocator), allocator);
  dev_node.AddMember("device", device_info(fd, allocator), allocator);

  drmModeRes* res = drm->ModeGetResources(fd);
  if (!res) {
    LOG_ERROR("[drmpp] drmModeGetResources failed");
    d.SetNull();
    return;
  }

  rapidjson::Value fb_size_obj(rapidjson::kObjectType);

  fb_size_obj.AddMember(
      "min_width", rapidjson::Value().SetUint64(res->min_width), allocator);
  fb_size_obj.AddMember(
      "max_width", rapidjson::Value().SetUint64(res->max_width), allocator);
  fb_size_obj.AddMember(
      "min_height", rapidjson::Value().SetUint64(res->min_height), allocator);
  fb_size_obj.AddMember(
      "max_height", rapidjson::Value().SetUint64(res->max_height), allocator);

  fb_size_obj.AddMember("fb_size", fb_size_obj,
                        allocator);  // typo? Reconsider structure

  dev_node.AddMember("connectors", connectors_info(fd, res, allocator),
                     allocator);
  dev_node.AddMember("encoders", encoders_info(fd, res, allocator), allocator);
  dev_node.AddMember("crtcs", crtcs_info(fd, res, allocator), allocator);
  dev_node.AddMember("planes", planes_info(fd, allocator).Move(), allocator);

  d.AddMember(key, dev_node, allocator);

  drm->ModeFreeResources(res);
}

std::string DrmInfo::get_node_info(const std::string& path) {
  rapidjson::Document document;
  document.SetObject();
  node_info(path.c_str(), document);
  rapidjson::StringBuffer sb;
  rapidjson::Writer writer(sb);
  document.Accept(writer);

  return sb.GetString();
}

std::string DrmInfo::get_node_info(const std::vector<std::string>& nodes) {
  rapidjson::Document document;
  document.SetObject();
  for (const auto& node : nodes) {
    node_info(node.c_str(), document);
  }
  rapidjson::StringBuffer sb;
  rapidjson::Writer writer(sb);
  document.Accept(writer);

  return sb.GetString();
}
}  // namespace drmpp::info
