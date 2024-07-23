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
static std::string& rtrim(std::string& s, const char* t) {
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
static std::string& ltrim(std::string& s, const char* t) {
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
[[maybe_unused]] static std::string& trim(std::string& s, const char* t) {
  return ltrim(rtrim(s, t), t);
}

/**
 * @brief Split string by token
 * @return std::vector<std::string>
 * @relation
 * internal
 */
[[maybe_unused]] static std::vector<std::string> split(
    std::string str,
    const std::string& token) {
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

static bool execute(const char* cmd, std::string& result) {
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

static bool is_cmd_present(const char* cmd) {
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

class IosFlagSaver {
 public:
  explicit IosFlagSaver(std::ostream& _ios) : ios(_ios), f(_ios.flags()) {}

  ~IosFlagSaver() { ios.flags(f); }

  IosFlagSaver(const IosFlagSaver& rhs) = delete;

  IosFlagSaver& operator=(const IosFlagSaver& rhs) = delete;

 private:
  std::ostream& ios;
  std::ios::fmtflags f;
};

template <unsigned RowSize, bool ShowAscii>
struct CustomHexdump {
  CustomHexdump(const uint8_t* data, size_t length)
      : mData(data), mLength(length) {}

  const uint8_t* mData;
  const size_t mLength;
};

template <unsigned row_size, bool show_ascii>
std::ostream& operator<<(std::ostream& out,
                         const CustomHexdump<row_size, show_ascii>& dump) {
  IosFlagSaver ios_fs(out);

  out.fill('0');
  for (size_t i = 0; i < dump.mLength; i += row_size) {
    out << "0x" << std::setw(6) << std::hex << i << ": ";
    for (size_t j = 0; j < row_size; ++j) {
      if (i + j < dump.mLength) {
        out << std::hex << std::setw(2) << static_cast<int>(dump.mData[i + j])
            << " ";
      } else {
        out << "   ";
      }
    }

    out << " ";
    if (show_ascii) {
      for (size_t j = 0; j < row_size; ++j) {
        if (i + j < dump.mLength) {
          if (std::isprint(dump.mData[i + j])) {
            out << static_cast<char>(dump.mData[i + j]);
          } else {
            out << ".";
          }
        }
      }
    }
    out << std::endl;
  }

  return out;
}

typedef CustomHexdump<16, true> Hexdump;
}  // namespace drmpp::utils
#endif  // INCLUDE_DRMPP_UTILS_H_
