/*
 * Copyright 2024 drmpp contributors
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

#ifndef INCLUDE_DRMPP_COMPOSITION_H_
#define INCLUDE_DRMPP_COMPOSITION_H_

#include <cassert>
#include <memory>
#include <optional>
#include <vector>

#include <xf86drm.h>
#include <xf86drmMode.h>

#include "buffer.h"

namespace drmpp {

struct Rect {
  uint32_t x;
  uint32_t y;
  uint32_t w;
  uint32_t h;
};

class Composition final {
 public:
  struct Layer {
    std::shared_ptr<Buffer> buffer;
    Rect src = {0, 0, 0, 0};
    Rect dst = {0, 0, 0, 0};
  };

  struct PointerLayer {
    std::shared_ptr<Buffer> buffer;
    uint32_t hot_x, hot_y;
    uint32_t x, y;
  };

  Composition() = default;
  ~Composition() = default;

  // Allow move, move-assign
  Composition(Composition&&) = default;
  Composition& operator=(Composition&&) = default;

  // Disallow copy and assign.
  Composition(const Composition&) = delete;
  Composition& operator=(const Composition&) = delete;

  /**
   * @brief Add a visible layer to the composition.
   *
   * More recently added layers will be rendered on top of older layers.
   */
  void addLayer(const Layer& layer) { layers_.push_back(layer); }

  /**
   * @brief Add a pointer layer to the composition, consisting of a pointer
   * image and a position. Only one pointer layer can be added to a composition.
   *
   * Adding a second pointer layer is considered an API usage error and might
   * result in an assertion failure.
   *
   * Presenting a Composition without a pointer layer will disable the pointer
   * on that output. (In other words, a pointer layer needs to be added to every
   * Composition that should display a pointer on screen.)
   *
   * TODO: Support out-of-band, higher-frequency updates via drmModeMoveCursor.
   */
  void addPointerLayer(const PointerLayer& layer) {
    assert(!pointer_.has_value());
    pointer_ = layer;
  }
  [[nodiscard]] const PointerLayer* pointerLayer() const {
    return pointer_.has_value() ? &pointer_.value() : nullptr;
  }

  // Layer iterator
  using const_iterator = std::vector<Layer>::const_iterator;
  [[nodiscard]] const_iterator begin() const { return layers_.begin(); }
  [[nodiscard]] const_iterator end() const { return layers_.end(); }

 private:
  std::optional<PointerLayer> pointer_;
  std::vector<Layer> layers_;
};

}  // namespace drmpp

#endif  // INCLUDE_DRMPP_COMPOSITION_H_
