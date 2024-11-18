
#include "input/pointer.h"

#include <algorithm>

#include "drmpp.h"

#include "input/left_ptr_default.h"

namespace drmpp::input {
/**
 * @brief Pointer class represents a libinput pointer device.
 *
 * The Pointer class is responsible for handling libinput pointer events and
 * managing the cursor.
 */
Pointer::Pointer(const bool disable_cursor,
                 event_mask const& event_mask,
                 const int size)
    : disable_cursor_(disable_cursor),
      size_(size),
      event_mask_({.enabled = event_mask.enabled,
                   .all = event_mask.all,
                   .axis = event_mask.axis,
                   .buttons = event_mask.buttons,
                   .motion = event_mask.motion}) {
  LOG_DEBUG("Pointer");

  event_mask_.enabled = event_mask.enabled;
  event_mask_.axis = event_mask.axis;
  event_mask_.buttons = event_mask.buttons;
  event_mask_.motion = event_mask.motion;

  if (!disable_cursor) {
    auto* buffer =
        static_cast<char*>(calloc(1, kCursor_LeftPtr_uncompressed_length));
    utils::asset_decompress(kCursor_LeftPtr, std::size(kCursor_LeftPtr),
                            reinterpret_cast<uint8_t*>(buffer));
    free(buffer);
  }
}

Pointer::~Pointer() = default;

void Pointer::set_cursor(uint32_t serial,
                         const char* cursor_name,
                         const char* theme_name) const {
  if (disable_cursor_) {
    return;
  }
#if 0
    if (!theme_) {
        theme_ = wl_cursor_theme_load(theme_name, size_, wl_shm_);
        if (!theme_) {
            LOG_ERROR("[Pointer] unable to load {} theme",
                          theme_name == nullptr ? "default" : theme_name);
            return;
        }
    }

    auto cursor = wl_cursor_theme_get_cursor(theme_, cursor_name);
    if (!cursor) {
        LOG_ERROR("[Pointer] unable to load {}", cursor_name);
        return;
    }
    auto image = cursor->images[0];
    auto buffer = wl_cursor_image_get_buffer(image);
    if (!buffer) {
        return;
    }
    wl_pointer_set_cursor(wl_pointer_, serial, wl_surface_cursor_,
                          static_cast<int32_t>(image->hotspot_x),
                          static_cast<int32_t>(image->hotspot_y));
    wl_surface_attach(wl_surface_cursor_, buffer, 0, 0);
    wl_surface_damage(wl_surface_cursor_, 0, 0,
                      static_cast<int32_t>(image->width),
                      static_cast<int32_t>(image->height));
    wl_surface_commit(wl_surface_cursor_);
#endif
}

std::string Pointer::get_cursor_theme() {
  std::string res;
  utils::execute("gsettings get org.gnome.desktop.interface cursor-theme", res);
  if (!res.empty()) {
    // clean up string
    std::string tmp = "\'\n";
    for_each(tmp.begin(), tmp.end(), [&res](char n) {
      res.erase(std::remove(res.begin(), res.end(), n), res.end());
    });
  }

  return res;
}

std::vector<std::string> Pointer::get_available_cursors(
    const char* theme_name) {
  std::string theme = theme_name == nullptr ? get_cursor_theme() : theme_name;

  std::ostringstream ss;
  ss << "ls -1 /usr/share/icons/" << theme << "/cursors";

  std::string res;
  utils::execute(ss.str(), res);

  std::vector<std::string> cursor_list;

  std::string line;
  std::istringstream orig_stream(res);
  while (std::getline(orig_stream, line)) {
    if (!line.empty())
      cursor_list.push_back(line);
  }

  std::sort(cursor_list.begin(), cursor_list.end());

  return cursor_list;
}

void Pointer::set_event_mask(event_mask const& event_mask) {
  event_mask_.enabled = event_mask.enabled;
  event_mask_.all = event_mask.all;
  event_mask_.axis = event_mask.axis;
  event_mask_.buttons = event_mask.buttons;
  event_mask_.motion = event_mask.motion;
}

void Pointer::handle_pointer_button_event(libinput_event_pointer* ev) {
  std::scoped_lock<std::mutex> lock(observers_mutex_);
  for (const auto observer : observers_) {
    observer->notify_pointer_button(
        this, 0, 0, libinput_event_pointer_get_button(ev),
        libinput_event_pointer_get_button_state(ev));
  }
}

void Pointer::handle_pointer_motion_event(libinput_event_pointer* ev) {
  std::scoped_lock<std::mutex> lock(observers_mutex_);
  for (const auto observer : observers_) {
    observer->notify_pointer_motion(this, 0, libinput_event_pointer_get_dx(ev),
                                    libinput_event_pointer_get_dy(ev));
  }
}

void Pointer::handle_pointer_axis_event(libinput_event_pointer* ev) {
  std::scoped_lock<std::mutex> lock(observers_mutex_);
  for (const auto observer : observers_) {
    observer->notify_pointer_axis_source(
        this, libinput_event_pointer_get_axis_source(ev));
  }
}

void Pointer::handle_pointer_motion_absolute_event(libinput_event_pointer* ev) {
  std::scoped_lock<std::mutex> lock(observers_mutex_);
  // TODO needs to reference surface size
  LOG_INFO("motion absolute: x: {}, y: {}",
           libinput_event_pointer_get_absolute_x_transformed(ev, 1024),
           libinput_event_pointer_get_absolute_y_transformed(ev, 768));
}
}  // namespace drmpp::input
