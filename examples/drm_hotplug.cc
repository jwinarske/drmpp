#include <csignal>
#include <cxxopts.hpp>
#include <filesystem>
#include <memory>
#include "drmpp/logging/logging.h"
#include "drmpp/utils/udev_monitor.h"
#include "drmpp/utils/utils.h"

class App final : public Logging, public drmpp::utils::UdevMonitor {
 public:
  explicit App()
      : UdevMonitor(
            "drm",
            [](const char* action,
               const char* dev_node,
               const char* sub_system,
               const char* dev_type) {
              LOG_INFO("action: {}, dev_node: {}, sub_system: {}, dev_type: {}",
                       action, dev_node, sub_system, dev_type);
            }) {}

  ~App() override = default;
};

std::unique_ptr<App> gApp;

void signal_handler(const int signal) {
  if (signal == SIGINT) {
    if (gApp) {
      gApp->stop();
    }
  }
}

int main(const int argc, char** argv) {
  std::signal(SIGINT, signal_handler);

  cxxopts::Options options("drm-hotplug", "monitor drm hotplug events");
  options.set_width(80)
      .set_tab_expansion()
      .allow_unrecognised_options()
      .add_options()("help", "Print help");

  if (options.parse(argc, argv).count("help")) {
    LOG_INFO("{}", options.help({"", "Group"}));
    exit(EXIT_SUCCESS);
  }

  gApp = std::make_unique<App>();
  std::thread monitor_thread(&App::run, gApp.get());
  monitor_thread.join();

  return EXIT_SUCCESS;
}