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

#include "buffer.h"

#include <cstring>

void drmpp::Buffer::fill(const uint32_t fourByteColor) {
  uint32_t offset, stride;
  std::size_t size;

  void* buffer = map(offset, stride, size, 0);
  if (!buffer) {
    return;
  }

  // fill the buffer, starting from buffer + offset to buffer + offset + size
  // with repeating 4-byte color
  for (std::size_t i = 0; i < size; i += 4) {
    std::memcpy(static_cast<uint8_t*>(buffer) + offset + i, &fourByteColor, 4);
  }

  unmap(buffer);
}
