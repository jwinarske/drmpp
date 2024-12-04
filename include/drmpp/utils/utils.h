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

#ifndef INCLUDE_DRMPP_UTILS_UTILS_H_
#define INCLUDE_DRMPP_UTILS_UTILS_H_

#include <algorithm>
#include <cstring>
#include <string>
#include <vector>

#include <cctype>
#include <cerrno>
#include <iomanip>
#include <ostream>

#include "libudev.h"

#include "logging/logging.h"

namespace drmpp::utils {
/**
 * \brief Trims the characters specified in `t` from the end of the string `s`.
 *
 * \param s The string to be trimmed.
 * \param t The characters to be trimmed from the end of the string.
 * \return The trimmed string.
 */
inline std::string& rtrim(std::string& s, const char* t) {
  s.erase(s.find_last_not_of(t) + 1);
  return s;
}

/**
 * \brief Trims the characters specified in `t` from the beginning of the string
 * `s`.
 *
 * \param s The string to be trimmed.
 * \param t The characters to be trimmed from the beginning of the string.
 * \return The trimmed string.
 */
inline std::string& ltrim(std::string& s, const char* t) {
  s.erase(0, s.find_first_not_of(t));
  return s;
}

/**
 * \brief Trims the characters specified in `t` from both ends of the string
 * `s`.
 *
 * \param s The string to be trimmed.
 * \param t The characters to be trimmed from both ends of the string.
 * \return The trimmed string.
 */
[[maybe_unused]] inline std::string& trim(std::string& s, const char* t) {
  return ltrim(rtrim(s, t), t);
}

/**
 * \brief Splits the string `str` by the delimiter `token`.
 *
 * \param str The string to be split.
 * \param token The delimiter used to split the string.
 * \return A vector of strings obtained by splitting the input string.
 */
[[maybe_unused]] inline std::vector<std::string> split(
    std::string str,
    const std::string& token) {
  std::vector<std::string> result;
  while (!str.empty()) {
    if (const auto index = str.find(token); index != std::string::npos) {
      result.push_back(str.substr(0, index));
      str = str.substr(index + token.size());
      if (str.empty())
        result.push_back(str);
    } else {
      result.push_back(str);
      str.clear();
    }
  }
  return result;
}

/**
 * \brief Checks if the character `c` is a safe character.
 *
 * Safe characters include alphanumeric characters, space, underscore, hyphen,
 * slash, and dot.
 *
 * \param c The character to be checked.
 * \return Non-zero if the character is safe, zero otherwise.
 */
inline int is_safe_char(const char c) {
  return std::isalnum(static_cast<unsigned char>(c)) || c == ' ' || c == '_' ||
         c == '-' || c == '/' || c == '.';
}

/**
 * \brief Sanitizes the command string `cmd` by removing unsafe characters.
 *
 * \param cmd The command string to be sanitized.
 * \return The sanitized command string.
 */
inline std::string sanitize_cmd(const std::string& cmd) {
  std::string safe_cmd;
  for (const char c : cmd) {
    if (is_safe_char(c)) {
      safe_cmd += c;
    }
  }
  return safe_cmd;
}

/**
 * \brief Executes the command `cmd` and stores the result in `result`.
 *
 * \param cmd The command to be executed.
 * \param result The string to store the result of the command execution.
 * \return True if the command was executed successfully, false otherwise.
 */
inline bool execute(const std::string& cmd, std::string& result) {
  if (cmd.empty()) {
    LOG_ERROR("execute: cmd is empty");
    return false;
  }
  const std::string safe_cmd = sanitize_cmd(cmd);
  FILE* fp = popen(safe_cmd.c_str(), "r");
  if (!fp) {
    LOG_ERROR("[ExecuteCommand] Failed to Execute Command: ({}) {}", errno,
              strerror(errno));
    LOG_ERROR("Failed to Execute Command: {}", cmd);
    return false;
  }

  DLOG_TRACE("[Command] Execute: {}", cmd);

  result.clear();
  auto buf = std::make_unique<char[]>(1024);
  while (fgets(buf.get(), 1024, fp) != nullptr) {
    result.append(buf.get());
  }
  buf.reset();

  DLOG_TRACE("[Command] Execute Result: [{}] {}", result.size(), result);

  if (pclose(fp) == -1) {
    LOG_ERROR("[ExecuteCommand] Failed to Close Pipe: ({}) {}", errno,
              strerror(errno));
    return false;
  }
  return true;
}

/**
 * \brief Checks if the command `cmd` is present in the system.
 *
 * \param cmd The command to be checked.
 * \return True if the command is present, false otherwise.
 */
inline bool is_cmd_present(const char* cmd) {
  const std::string check_cmd = "which " + std::string(cmd);
  if (std::string result; execute(check_cmd, result)) {
    if (result.empty()) {
      return false;
    }
    return true;
  }
  return false;
}

/**
 * \brief Retrieves the udev framebuffer system attributes.
 *
 * \return An unordered map containing the udev framebuffer system attributes.
 */
inline std::unordered_map<std::string, std::string>
get_udev_fb_sys_attributes() {
  std::unordered_map<std::string, std::string> results;
  const auto udev = udev_new();
  if (!udev) {
    LOG_ERROR("Can't create udev");
    return results;
  }

  const auto enumerate = udev_enumerate_new(udev);
  udev_enumerate_add_match_subsystem(enumerate, "graphics");
  udev_enumerate_scan_devices(enumerate);

  const auto devices = udev_enumerate_get_list_entry(enumerate);
  udev_list_entry* dev_list_entry;
  udev_list_entry_foreach(dev_list_entry, devices) {
    const auto path = udev_list_entry_get_name(dev_list_entry);
    LOG_DEBUG("path: {}", path);
    if (strcmp(path, "fbcon") == 0) {
      continue;
    }
    const auto dev = udev_device_new_from_syspath(udev, path);

    const auto properties_list = udev_device_get_properties_list_entry(dev);
    udev_list_entry* properties_list_entry;
    udev_list_entry_foreach(properties_list_entry, properties_list) {
      const auto properties_name =
          udev_list_entry_get_name(properties_list_entry);
      if (properties_name) {
        const auto value = udev_device_get_property_value(dev, properties_name);
        results[properties_name] = value ? value : "";
      }
    }

    const auto sys_attr_list = udev_device_get_sysattr_list_entry(dev);
    udev_list_entry* sys_attr_list_entry;
    udev_list_entry_foreach(sys_attr_list_entry, sys_attr_list) {
      if (const auto sys_attr = udev_list_entry_get_name(sys_attr_list_entry)) {
        const auto value = udev_device_get_sysattr_value(dev, sys_attr);
        results[sys_attr] = value ? value : "";
      }
    }
    udev_device_unref(dev);
  }
  udev_unref(udev);
  return results;
}

/**
 * \brief Retrieves and logs the udev system attributes for the specified
 * subsystem.
 *
 * \param subsystem The subsystem to retrieve the attributes for.
 */
inline void get_udev_sys_attributes(const char* subsystem) {
  const auto udev = udev_new();
  if (!udev) {
    LOG_ERROR("Can't create udev");
    return;
  }

  LOG_INFO("=============================");
  LOG_INFO("{}:", subsystem);
  const auto enumerate = udev_enumerate_new(udev);
  udev_enumerate_add_match_subsystem(enumerate, subsystem);
  udev_enumerate_scan_devices(enumerate);

  const auto devices = udev_enumerate_get_list_entry(enumerate);
  udev_list_entry* dev_list_entry;
  udev_list_entry_foreach(dev_list_entry, devices) {
    const auto path = udev_list_entry_get_name(dev_list_entry);
    const auto dev = udev_device_new_from_syspath(udev, path);

    LOG_INFO("* Device Node");
    if (const auto devnode = udev_device_get_devnode(dev)) {
      LOG_INFO("devnode: {}", devnode);
    } else {
      const auto parent = udev_device_get_parent(dev);
      if (const auto parent_path = udev_device_get_syspath(parent)) {
        if (const auto parent_devnode = udev_device_get_devnode(parent)) {
          LOG_INFO("parent: {}", parent_devnode);
        } else {
          LOG_INFO("parent: {}", parent_path);
        }
      }
    }
    LOG_INFO("* Properties");
    const auto properties_list = udev_device_get_properties_list_entry(dev);
    udev_list_entry* properties_list_entry;
    udev_list_entry_foreach(properties_list_entry, properties_list) {
      const auto properties_name =
          udev_list_entry_get_name(properties_list_entry);
      if (properties_name) {
        const auto value = udev_device_get_property_value(dev, properties_name);
        LOG_INFO("{}: {}", properties_name, value ? value : "");
      }
    }

    LOG_INFO("* System Attributes");
    const auto sys_attr_list = udev_device_get_sysattr_list_entry(dev);
    udev_list_entry* sys_attr_list_entry;
    udev_list_entry_foreach(sys_attr_list_entry, sys_attr_list) {
      if (const auto sys_attr = udev_list_entry_get_name(sys_attr_list_entry)) {
        const auto value = udev_device_get_sysattr_value(dev, sys_attr);
        LOG_INFO("sys_attr: {}: {}", sys_attr, value ? value : "");
      }
    }

    udev_device_unref(dev);
  }
  udev_enumerate_unref(enumerate);
  udev_unref(udev);
}

/**
 * \brief Retrieves a list of DRM (Direct Rendering Manager) device nodes.
 *
 * This function uses the udev library to enumerate and collect DRM device nodes
 * from the /dev/dri/ directory. Specifically, it collects nodes that match the
 * pattern "/dev/dri/card*" and excludes nodes that match the pattern
 * "/dev/dri/render*". The resulting list of device nodes is sorted before being
 * returned.
 *
 * \return A sorted vector of strings, each representing a DRM device node path.
 */
inline std::vector<std::string> get_drm_nodes() {
  std::vector<std::string> card_nodes;

  udev* udev = udev_new();
  if (!udev) {
    LOG_ERROR("Cannot create udev object");
    return card_nodes;
  }

  udev_enumerate* enumerate = udev_enumerate_new(udev);
  if (!enumerate) {
    LOG_ERROR("Cannot create udev enumerate object");
    udev_unref(udev);
    return card_nodes;
  }

  udev_enumerate_add_match_subsystem(enumerate, "drm");
  udev_enumerate_scan_devices(enumerate);

  udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
  udev_list_entry* entry;

  udev_list_entry_foreach(entry, devices) {
    const char* path = udev_list_entry_get_name(entry);
    udev_device* device = udev_device_new_from_syspath(udev, path);

    if (const char* dev_node = udev_device_get_devnode(device)) {
      if (std::string node(dev_node);
          node.find("/dev/dri/card") == 0 &&
          node.find("/dev/dri/render") == std::string::npos) {
        card_nodes.push_back(node);
      }
    }

    udev_device_unref(device);
  }

  udev_enumerate_unref(enumerate);
  udev_unref(udev);

  // Sort the resulting vector
  std::sort(card_nodes.begin(), card_nodes.end());

  return card_nodes;
}

/**
 * \brief Retrieves a list of enabled DRM (Direct Rendering Manager) nodes.
 *
 * This method interacts with the udev library to enumerate and identify
 * enabled DRM nodes on the system. It creates and manages udev objects
 * and retrieves device paths for all enabled DRM devices.
 *
 * \return A vector of strings, where each string represents the device path
 *         of an enabled DRM node.
 */
inline std::vector<std::string> get_enabled_drm_nodes() {
  std::vector<std::string> enabled_devices;

  udev* udev = udev_new();
  if (!udev) {
    LOG_ERROR("Cannot create udev object");
    return enabled_devices;
  }

  udev_enumerate* enumerate = udev_enumerate_new(udev);
  if (!enumerate) {
    LOG_ERROR("Cannot create udev enumerate object");
    udev_unref(udev);
    return enabled_devices;
  }

  udev_enumerate_scan_devices(enumerate);

  udev_list_entry* devices = udev_enumerate_get_list_entry(enumerate);
  udev_list_entry* entry;

  udev_list_entry_foreach(entry, devices) {
    const char* path = udev_list_entry_get_name(entry);
    udev_device* device = udev_device_new_from_syspath(udev, path);
    if (const char* enabled = udev_device_get_sysattr_value(device, "enabled");
        enabled && std::string(enabled) == "enabled") {
      if (udev_device* parent_device = udev_device_get_parent(device)) {
        if (const char* dev_node = udev_device_get_devnode(parent_device)) {
          if (std::find(enabled_devices.begin(), enabled_devices.end(),
                        dev_node) == enabled_devices.end()) {
            enabled_devices.emplace_back(dev_node);
          }
        }
      }
    }
    udev_device_unref(device);
  }

  udev_enumerate_unref(enumerate);
  udev_unref(udev);

  // Sort the resulting vector
  std::sort(enabled_devices.begin(), enabled_devices.end());

  return enabled_devices;
}

/**
 * \brief Retrieves the enabled DRM output nodes.
 *
 * \param connected If true, only connected nodes are retrieved.
 * \return A vector of strings containing the enabled DRM output nodes.
 */
inline std::vector<std::string> get_enabled_drm_output_nodes(
    const bool connected) {
  std::vector<std::string> result;

  const auto udev = udev_new();
  if (!udev) {
    LOG_ERROR("Can't create udev");
    return {};
  }

  const auto enumerate = udev_enumerate_new(udev);
  udev_enumerate_add_match_subsystem(enumerate, "drm");
  udev_enumerate_scan_devices(enumerate);

  const auto devices = udev_enumerate_get_list_entry(enumerate);
  udev_list_entry* dev_list_entry;
  udev_list_entry_foreach(dev_list_entry, devices) {
    const auto path = udev_list_entry_get_name(dev_list_entry);
    const auto dev = udev_device_new_from_syspath(udev, path);

    if (!udev_device_get_devnode(dev)) {
      if (strcmp(udev_device_get_sysattr_value(dev, "enabled"), "enabled") ==
          0) {
        if (connected) {
          if (strcmp(udev_device_get_sysattr_value(dev, "status"),
                     "connected") == 0) {
            result.emplace_back(path);
          }
        } else {
          result.emplace_back(path);
        }
      }
    }
    udev_device_unref(dev);
  }
  udev_enumerate_unref(enumerate);
  udev_unref(udev);

  return result;
}

/**
 * \brief Saves the current I/O stream flags and restores them upon
 * destruction.
 */
class IosFlagSaver {
 public:
  /**
   * \brief Constructs an IosFlagSaver and saves the current I/O stream flags.
   *
   * \param _ios The I/O stream to save the flags from.
   */
  explicit IosFlagSaver(std::ostream& _ios) : ios(_ios), f(_ios.flags()) {}

  /**
   * \brief Restores the saved I/O stream flags.
   */
  ~IosFlagSaver() { ios.flags(f); }

  IosFlagSaver(const IosFlagSaver& rhs) = delete;

  IosFlagSaver& operator=(const IosFlagSaver& rhs) = delete;

 private:
  std::ostream& ios;
  std::ios::fmtflags f;
};

/**
 * \brief Struct for custom hex header dump.
 *
 * \tparam RowSize The size of each row in the hex dump.
 */
template <unsigned RowSize>
struct CustomHexHeaderdump {
  CustomHexHeaderdump(const uint8_t* data, const size_t length)
      : mData(data), mLength(length) {}

  const uint8_t* mData;
  const size_t mLength;
};

/**
 * \brief Overloads the output stream operator to print a custom hex header
 * dump.
 *
 * \tparam RowSize The size of each row in the hex dump.
 * \param out The output stream.
 * \param dump The custom hex header dump to be printed.
 * \return The output stream.
 */
template <unsigned RowSize>
std::ostream& operator<<(std::ostream& out,

                         const CustomHexHeaderdump<RowSize>& dump) {
  IosFlagSaver ios_fs(out);

  out.fill('0');
  for (size_t i = 0; i < dump.mLength; i += RowSize) {
    for (size_t j = 0; j < RowSize; ++j) {
      if (i + j < dump.mLength) {
        out << "0x" << std::hex << std::setw(2)
            << static_cast<int>(dump.mData[i + j]);
        if (i + j != (dump.mLength - 1)) {
          out << ", ";
        }
      }
    }
    if (i == dump.mLength - 1) {
      out << "\"" << std::endl;
    } else {
      out << std::endl;
    }
  }

  return out;
}

/**
 * \brief Decompresses LZ compressed data.
 *
 * This function decompresses data that has been compressed using a simple LZ
 * (Lempel-Ziv) compression algorithm. It handles literal runs, short matches,
 * and long matches to reconstruct the original uncompressed data.
 *
 * \param data The compressed data to be decompressed.
 * \param compressed_size The size of the compressed data.
 * \param uncompressed_size The expected size of the uncompressed data.
 * \return A vector containing the decompressed data.
 */
inline std::vector<uint8_t> decompress_lz_asset(
    const uint8_t data[],
    const size_t compressed_size,
    const size_t uncompressed_size) {
  int src = 0;
  int dest = 0;

  const uint8_t* input = data;
  const int length = compressed_size;
  std::vector<uint8_t> buffer(uncompressed_size, 0);

  uint8_t* output = buffer.data();

  while (src < length) {
    if (const int type = input[src] >> 5; type == 0) {
      // literal run
      int run = 1 + input[src];
      src = src + 1;
      while (run > 0) {
        output[dest] = input[src];
        src = src + 1;
        dest = dest + 1;
        run = run - 1;
      }
    } else if (type < 7) {
      // short match
      const int ofs = 256 * (input[src] & 31) + input[src + 1];
      int len = 2 + (input[src] >> 5);
      src = src + 2;
      int ref = dest - ofs - 1;
      while (len > 0) {
        output[dest] = output[ref];
        ref = ref + 1;
        dest = dest + 1;
        len = len - 1;
      }
    } else {
      // long match
      const int ofs = 256 * (input[src] & 31) + input[src + 2];
      int len = 9 + input[src + 1];
      src = src + 3;
      int ref = dest - ofs - 1;
      while (len > 0) {
        output[dest] = output[ref];
        ref = ref + 1;
        dest = dest + 1;
        len = len - 1;
      }
    }
  }
  assert(static_cast<int>(uncompressed_size) == dest);
  LOG_INFO("lz asset compressed: {}, decompressed: {}", compressed_size, dest);
  return buffer;
}
}  // namespace drmpp::utils
#endif  // INCLUDE_DRMPP_UTILS_UTILS_H_
