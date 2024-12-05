#include <unistd.h>
#include <functional>
#include <libudev.h>
#include "drmpp/logging/logging.h"

namespace drmpp::utils {

/**
 * \brief Class for monitoring udev events.
 */
class UdevMonitor {
public:
  /**
   * \brief Constructor for UdevMonitor.
   *
   * \param sub_system The subsystem to monitor.
   * \param callback The callback function to call with device information.
   */
  UdevMonitor(const char* sub_system, const std::function<void(const char*, const char*, const char*, const char*)>& callback);

  /**
   * \brief Destructor for UdevMonitor.
   */
  virtual ~UdevMonitor();

  /**
   * \brief Starts the udev monitoring.
   */
  void run() const;

  /**
   * \brief Stops the udev monitoring.
   */
  void stop();

private:
  std::string sub_system_; /**< The subsystem to monitor */

  std::atomic<bool> running_ = {true}; /**< Atomic flag to control the running state */

  int pipe_fds_[2]{}; /**< Pipe file descriptors for signaling */

  /**< Callback function to call with device information */
  const std::function<void(const char *action,
                           const char *dev_node,
                           const char *sub_system,
                           const char *dev_type)>& callback_;
};

}  // namespace drmpp::utils