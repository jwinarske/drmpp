#ifndef INCLUDE_DRMPP_UTILS_H_
#define INCLUDE_DRMPP_UTILS_H_

#include <string>
#include <vector>

#include <cctype>
#include <cerrno>
#include <iomanip>
#include <ostream>

#include "drmpp.h"

namespace drmpp::utils {
  /**
   * @brief trim from end of string (right)
   * @return std::string&
   * @retval String that has specified characters trimmed from right.
   * @relation
   * flutter
   */
  inline std::string &rtrim(std::string &s, const char *t) {
    s.erase(s.find_last_not_of(t) + 1);
    return s;
  }

  /**
   * @brief trim from beginning of string (left)
   * @return std::string&
   * @retval String that has specified characters trimmed from left.
   * @relation
   * flutter
   */
  inline std::string &ltrim(std::string &s, const char *t) {
    s.erase(0, s.find_first_not_of(t));
    return s;
  }

  /**
   * @brief trim from both ends of string (right then left)
   * @return std::string&
   * @retval String that has specified characters trimmed from right and left.
   * @relation
   * flutter
   */
  [[maybe_unused]] inline std::string &trim(std::string &s, const char *t) {
    return ltrim(rtrim(s, t), t);
  }

  /**
   * @brief Split string by token
   * @return std::vector<std::string>
   * @relation
   * internal
   */
  [[maybe_unused]] inline std::vector<std::string> split(
    std::string str,
    const std::string &token) {
    std::vector<std::string> result;
    while (!str.empty()) {
      const auto index = str.find(token);
      if (index != std::string::npos) {
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

  inline int is_safe_char(const char c) {
    // Only allow alphanumeric characters and a few safe symbols
    return std::isalnum(static_cast<unsigned char>(c)) || c == '_' || c == '-' || c == '/' || c == '.';
  }

  inline std::string sanitize_cmd(const std::string &cmd) {
    std::string safe_cmd;
    for (const char c: cmd) {
      if (is_safe_char(c)) {
        safe_cmd += c;
      }
    }
    return safe_cmd;
  }

  inline bool execute(const std::string &cmd, std::string &result) {
    DLOG_TRACE("execute: cmd: {}", cmd);
    if (cmd.empty()) {
      spdlog::error("execute: cmd is empty");
      return false;
    }
    const std::string safe_cmd = sanitize_cmd(cmd);
    const auto fp = popen(safe_cmd.c_str(), "r");
    if (!fp) {
      LOG_ERROR("Failed to execute cmd: {}, ({}) {}", cmd, errno,
                strerror(errno));
      return false;
    }

    auto buffer = std::make_unique<char>(1024);
    while (std::fgets(buffer.get(), 1024, fp)) {
      result.append(buffer.get());
    }
    buffer.reset();
    DLOG_TRACE("execute: result: ({}) {}", result.size(), result);

    if (pclose(fp) == -1) {
      LOG_ERROR("Failed to Close Pipe: ({}) {}", errno, strerror(errno));
      return false;
    }
    return true;
  }

  inline bool is_cmd_present(const char *cmd) {
    const std::string check_cmd = "which " + std::string(cmd);
    std::string result;
    if (execute(check_cmd.c_str(), result)) {
      if (result.empty()) {
        return false;
      }
      return true;
    }
    return false;
  }

  inline std::unordered_map<std::string, std::string> get_udev_fb_sys_attributes() {
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
        const auto properties_name = udev_list_entry_get_name(properties_list_entry);
        if (properties_name) {
          const auto value = udev_device_get_property_value(dev, properties_name);
          results[properties_name] = value ? value : "";
        }
      }

      const auto sys_attr_list = udev_device_get_sysattr_list_entry(dev);
      udev_list_entry *sys_attr_list_entry;
      udev_list_entry_foreach(sys_attr_list_entry, sys_attr_list) {
        const auto sys_attr = udev_list_entry_get_name(sys_attr_list_entry);
        if (sys_attr) {
          const auto value = udev_device_get_sysattr_value(dev, sys_attr);
          results[sys_attr] = value ? value : "";
        }
      }
      udev_device_unref(dev);
    }
    udev_unref(udev);
    return results;
  }

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
      const auto devnode = udev_device_get_devnode(dev);
      if (devnode) {
        LOG_INFO("devnode: {}", devnode);
      } else {
        const auto parent = udev_device_get_parent(dev);
        const auto parent_path = udev_device_get_syspath(parent);
        if (parent_path) {
          const auto parent_devnode = udev_device_get_devnode(parent);
          if (parent_devnode) {
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
        const auto properties_name = udev_list_entry_get_name(properties_list_entry);
        if (properties_name) {
          const auto value = udev_device_get_property_value(dev, properties_name);
          LOG_INFO("{}: {}", properties_name, value ? value : "");
        }
      }

      LOG_INFO("* System Attributes");
      const auto sys_attr_list = udev_device_get_sysattr_list_entry(dev);
      udev_list_entry *sys_attr_list_entry;
      udev_list_entry_foreach(sys_attr_list_entry, sys_attr_list) {
        const auto sys_attr = udev_list_entry_get_name(sys_attr_list_entry);
        if (sys_attr) {
          const auto value = udev_device_get_sysattr_value(dev, sys_attr);
          LOG_INFO("sys_attr: {}: {}", sys_attr, value ? value : "");
        }
      }

      udev_device_unref(dev);
    }
    udev_enumerate_unref(enumerate);
    udev_unref(udev);
  }

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
        if (strcmp(udev_device_get_sysattr_value(dev, "enabled"), "enabled") == 0) {
          const auto parent = udev_device_get_parent(dev);
          auto parent_node = udev_device_get_devnode(parent);
          if (connected && strcmp(udev_device_get_sysattr_value(dev, "status"), "connected")
              ==
              0
          ) {
            result.emplace_back(parent_node);
          } else {
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

  inline std::vector<std::string> get_enabled_drm_output_nodes(const bool connected) {
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
        if (strcmp(udev_device_get_sysattr_value(dev, "enabled"), "enabled") == 0) {
          if (connected && strcmp(udev_device_get_sysattr_value(dev, "status"), "connected")
              ==
              0
          ) {
            result.emplace_back(path);
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

  class IosFlagSaver {
  public:
    explicit IosFlagSaver(std::ostream &_ios) : ios(_ios), f(_ios.flags()) {
    }

    ~IosFlagSaver() { ios.flags(f); }

    IosFlagSaver(const IosFlagSaver &rhs) = delete;

    IosFlagSaver &operator=(const IosFlagSaver &rhs) = delete;

  private:
    std::ostream &ios;
    std::ios::fmtflags f;
  };

  template
  <
    unsigned RowSize
  >
  struct CustomHexHeaderdump {
    CustomHexHeaderdump(const uint8_t *data, size_t length)
      : mData(data), mLength(length) {
    }

    const uint8_t *mData;
    const size_t mLength;
  };

  template
  <
    unsigned RowSize
  >
  std::ostream &operator<<(std::ostream &out,

                           const CustomHexHeaderdump<RowSize> &dump
  ) {
    IosFlagSaver ios_fs(out);

    out.fill('0');
    for (size_t i = 0; i < dump.mLength; i += RowSize) {
      for (size_t j = 0; j < RowSize; ++j) {
        if (i + j < dump.mLength) {
          out << "0x" << std::hex << std::setw(2) << static_cast<int>(dump.mData[i + j]);
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

  inline void asset_decompress(const uint8_t *input, const int length, uint8_t *output) {
    int src = 0;
    int dest = 0;
    while (src < length) {
      int type = input[src] >> 5;
      if (type == 0) {
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
  }
} // namespace drmpp::utils
#endif  // INCLUDE_DRMPP_UTILS_H_
