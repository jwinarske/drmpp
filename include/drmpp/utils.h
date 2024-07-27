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

  inline bool execute(const char *cmd, std::string &result) {
    DLOG_TRACE("execute: cmd: {}", cmd);
    const auto fp = popen(cmd, "r");
    if (!fp) {
      LOG_ERROR("Failed to execute cmd: {}, ({}) {}", cmd, errno,
                strerror(errno));
      return false;
    }

    char buffer[100]{};
    std::rewind(fp);
    while (std::fgets(buffer, sizeof(buffer), fp)) {
      result.append(buffer);
    }
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
          if (connected && strcmp(udev_device_get_sysattr_value(dev, "status"), "connected") == 0) {
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
          if (connected && strcmp(udev_device_get_sysattr_value(dev, "status"), "connected") == 0) {
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
} // namespace drmpp::utils
#endif  // INCLUDE_DRMPP_UTILS_H_
