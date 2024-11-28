/*
 * Copyright (c) 2024 The drmpp Contributors
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

#ifndef INCLUDE_DRMPP_INPUT_ASSET_H
#define INCLUDE_DRMPP_INPUT_ASSET_H

#include <cstdint>
#include <vector>

#include "fastlz.h"

template <size_t CompressedSize, size_t UncompressedSize>
struct Asset {
  size_t compressed_size = CompressedSize;
  size_t uncompressed_size = UncompressedSize;
  uint8_t data[CompressedSize];

  std::vector<uint8_t> decompress() const {
    std::vector<uint8_t> v;

    v.resize(uncompressed_size);

    int result = ::fastlz_decompress(data, compressed_size, v.data(), v.size());
    if (result == 0) {
      return {};
    } else if (static_cast<size_t>(result) != uncompressed_size) {
      return {};
    }

    return v;
  }
};

#endif  // INCLUDE_DRMPP_INPUT_ASSET_H
