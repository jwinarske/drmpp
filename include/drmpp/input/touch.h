#ifndef INCLUDE_DRMPP_INPUT_TOUCH_H_
#define INCLUDE_DRMPP_INPUT_TOUCH_H_

class Touch {
 public:
  Touch() = default;
  ~Touch() = default;

  // Disallow copy and assign.
  Touch(const Touch&) = delete;

  Touch& operator=(const Touch&) = delete;
};

#endif  // INCLUDE_DRMPP_INPUT_TOUCH_H_