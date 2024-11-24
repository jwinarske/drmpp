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

#ifndef INCLUDE_DRMPP_SHARED_LIBS_SHARED_LIBRARY_H
#define INCLUDE_DRMPP_SHARED_LIBS_SHARED_LIBRARY_H

#include <dlfcn.h>

#include "logging/logging.h"

template <typename FunctionPointer>
void GetFuncAddress(void* library,
                    const char* function_name,
                    FunctionPointer* out) {
  auto symbol = dlsym(library, function_name);
  if (!symbol) {
    const char* reason = dlerror();
    LOG_DEBUG("GetFuncAddress: {} - {}", function_name, reason);
  }
  *out = reinterpret_cast<FunctionPointer>(symbol);
}

#endif  // INCLUDE_DRMPP_SHARED_LIBS_SHARED_LIBRARY_H