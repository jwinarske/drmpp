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

#ifndef INCLUDE_DRMPP_UTILS_H_
#define INCLUDE_DRMPP_UTILS_H_

#include <cstring>
#include <string>
#include <vector>

#include <cctype>
#include <cerrno>
#include <iomanip>
#include <ostream>

#include "drmpp.h"

#include <input/asset.h>

namespace drmpp::utils {
  /**
   * \brief Trims the characters specified in `t` from the end of the string `s`.
   *
   * \param s The string to be trimmed.
   * \param t The characters to be trimmed from the end of the string.
   * \return The trimmed string.
   */
  inline std::string &rtrim(std::string &s, const char *t) {
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
  inline std::string &ltrim(std::string &s, const char *t) {
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
  [[maybe_unused]] inline std::string &trim(std::string &s, const char *t) {
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
    const std::string &token) {
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
  inline std::string sanitize_cmd(const std::string &cmd) {
    std::string safe_cmd;
    for (const char c: cmd) {
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
  inline bool execute(const std::string &cmd, std::string &result) {
    if (cmd.empty()) {
      LOG_ERROR("execute: cmd is empty");
      return false;
    }
    const std::string safe_cmd = sanitize_cmd(cmd);
    FILE *fp = popen(safe_cmd.c_str(), "r");
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
  inline bool is_cmd_present(const char *cmd) {
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
    udev_list_entry *dev_list_entry;
    udev_list_entry_foreach(dev_list_entry, devices) {
      const auto path = udev_list_entry_get_name(dev_list_entry);
      LOG_DEBUG("path: {}", path);
      if (strcmp(path, "fbcon") == 0) {
        continue;
      }
      const auto dev = udev_device_new_from_syspath(udev, path);

      const auto properties_list = udev_device_get_properties_list_entry(dev);
      udev_list_entry *properties_list_entry;
      udev_list_entry_foreach(properties_list_entry, properties_list) {
        const auto properties_name =
            udev_list_entry_get_name(properties_list_entry);
        if (properties_name) {
          const auto value = udev_device_get_property_value(dev, properties_name);
          results[properties_name] = value ? value : "";
        }
      }

      const auto sys_attr_list = udev_device_get_sysattr_list_entry(dev);
      udev_list_entry *sys_attr_list_entry;
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
  inline void get_udev_sys_attributes(const char *subsystem) {
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
    udev_list_entry *dev_list_entry;
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
      udev_list_entry *properties_list_entry;
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
      udev_list_entry *sys_attr_list_entry;
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
   * \brief Retrieves the enabled DRM nodes.
   *
   * \param connected If true, only connected nodes are retrieved.
   * \return A vector of strings containing the enabled DRM nodes.
   */
  inline std::vector<std::string> get_enabled_drm_nodes(const bool connected) {
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
    udev_list_entry *dev_list_entry;
    udev_list_entry_foreach(dev_list_entry, devices) {
      const auto path = udev_list_entry_get_name(dev_list_entry);
      const auto dev = udev_device_new_from_syspath(udev, path);
      if (!udev_device_get_devnode(dev)) {
        if (strcmp(udev_device_get_sysattr_value(dev, "enabled"), "enabled") ==
            0) {
          const auto parent = udev_device_get_parent(dev);
          auto parent_node = udev_device_get_devnode(parent);
          if (connected && strcmp(udev_device_get_sysattr_value(dev, "status"),
                                  "connected") == 0) {
            result.emplace_back(parent_node);
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
    udev_list_entry *dev_list_entry;
    udev_list_entry_foreach(dev_list_entry, devices) {
      const auto path = udev_list_entry_get_name(dev_list_entry);
      const auto dev = udev_device_new_from_syspath(udev, path);

      if (!udev_device_get_devnode(dev)) {
        if (strcmp(udev_device_get_sysattr_value(dev, "enabled"), "enabled") ==
            0) {
          if (connected && strcmp(udev_device_get_sysattr_value(dev, "status"),
                                  "connected") == 0) {
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
   * \brief Saves the current I/O stream flags and restores them upon destruction.
   */
  class IosFlagSaver {
  public:
    /**
     * \brief Constructs an IosFlagSaver and saves the current I/O stream flags.
     *
     * \param _ios The I/O stream to save the flags from.
     */
    explicit IosFlagSaver(std::ostream &_ios) : ios(_ios), f(_ios.flags()) {
    }

    /**
     * \brief Restores the saved I/O stream flags.
     */
    ~IosFlagSaver() { ios.flags(f); }

    IosFlagSaver(const IosFlagSaver &rhs) = delete;

    IosFlagSaver &operator=(const IosFlagSaver &rhs) = delete;

  private:
    std::ostream &ios;
    std::ios::fmtflags f;
  };

  /**
   * \brief Struct for custom hex header dump.
   *
   * \tparam RowSize The size of each row in the hex dump.
   */
  template<unsigned RowSize>
  struct CustomHexHeaderdump {
    CustomHexHeaderdump(const uint8_t *data, const size_t length)
      : mData(data), mLength(length) {
    }

    const uint8_t *mData;
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
  template<unsigned RowSize>
  std::ostream &operator<<(std::ostream &out,

                           const CustomHexHeaderdump<RowSize> &dump) {
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
   * \brief Decompresses an asset using a custom decompression algorithm.
   *
   * This function takes a compressed asset represented as a pair of size and a
   * vector of bytes, and decompresses it into a unique pointer to an array of
   * bytes. The decompression algorithm handles literal runs, short matches, and
   * long matches based on the input data.
   *
   * \param asset A pair containing the size of the decompressed asset and a
   * vector of bytes representing the compressed asset.
   * \return A unique pointer to an array of bytes containing the decompressed
   * asset.
   */
  inline std::vector<uint8_t> decompress_asset(const Asset &asset) {
    int src = 0;
    int dest = 0;

    const uint8_t *input = asset.data;
    const int length = asset.compressed_size;
    std::vector<uint8_t> buffer(asset.uncompressed_size, 0);

    uint8_t *output = buffer.data();

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
    assert(static_cast<int>(asset.uncompressed_size) == dest);
    LOG_INFO("asset compressed: {}, decompressed: {}", asset.compressed_size,
             dest);
    return buffer;
  }
} // namespace drmpp::utils
#endif  // INCLUDE_DRMPP_UTILS_H_
