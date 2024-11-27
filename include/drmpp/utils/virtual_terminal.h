#ifndef INCLUDE_DRMPP_UTILS_H_
#define INCLUDE_DRMPP_UTILS_H_

#include <termios.h>

namespace drmpp::utils {
  /**
   * \class VirtualTerminal
   * \brief Manages the virtual terminal settings for a DRM application.
   *
   * This class handles switching the terminal to graphics mode and restoring
   * it back to text mode upon exit or signal reception.
   */
  class VirtualTerminal {
  public:
    /**
     * \brief Constructs a VirtualTerminal object and switches the terminal to graphics mode.
     */
    VirtualTerminal();

    /**
     * \brief Destructs the VirtualTerminal object and restores the terminal to text mode.
     */
    virtual ~VirtualTerminal();

    /**
     * \brief Restores the terminal to text mode.
     *
     * This static function is called upon exit or signal reception to ensure
     * the terminal is properly restored to its original state.
     */
    static void restore();

    // Delete copy constructor
    VirtualTerminal(const VirtualTerminal &) = delete;

    // Delete copy assignment operator
    VirtualTerminal &operator=(const VirtualTerminal &) = delete;

  private:
    static termios gPreviousTio;
  };
} // namespace drmpp::utils

#endif  // INCLUDE_DRMPP_UTILS_H_
