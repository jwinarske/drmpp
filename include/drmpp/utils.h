#ifndef INCLUDE_DRMPP_UTILS_H_
#define INCLUDE_DRMPP_UTILS_H_

#include <string>
#include <vector>

#include <cerrno>

#include "drmpp.h"

namespace drmpp::utils {
  /**
   * @brief trim from end of string (right)
   * @return std::string&
   * @retval String that has specified characters trimmed from right.
   * @relation
   * flutter
   */
  static std::string &rtrim(std::string &s, const char *t) {
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
  static std::string &ltrim(std::string &s, const char *t) {
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
  [[maybe_unused]] static std::string &trim(std::string &s, const char *t) {
    return ltrim(rtrim(s, t), t);
  }


  /**
   * @brief Split string by token
   * @return std::vector<std::string>
   * @relation
   * internal
   */
  [[maybe_unused]] static std::vector<std::string> split(std::string str, const std::string &token) {
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

  static bool execute(const char *cmd, std::string &result) {
    const auto fp = popen(cmd, "r");
    if (!fp) {
      LOG_ERROR("[ExecuteCommand] Failed to Execute Command: ({}) {}", errno,
                strerror(errno));
      spdlog::error("Failed to Execute Command: {}", cmd);
      return false;
    }

    DLOG_TRACE("[Command] Execute: {}", cmd);

    auto buf = std::make_unique<char[]>(1024);
    while (fgets(&buf[0], 1024, fp) != nullptr) {
      result.append(&buf[0]);
    }
    buf.reset();

    DLOG_TRACE("[Command] Execute Result: [{}] {}", result.size(), result);

    auto status = pclose(fp);
    if (status == -1) {
      LOG_ERROR("[ExecuteCommand] Failed to Close Pipe: ({}) {}", errno,
                strerror(errno));
      return false;
    }
    return true;
  }

  static bool is_cmd_present(const char *cmd) {
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
}
#endif // INCLUDE_DRMPP_UTILS_H_
