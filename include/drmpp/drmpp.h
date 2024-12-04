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

#ifndef INCLUDE_DRMPP_H_
#define INCLUDE_DRMPP_H_

#include <fcntl.h>
#include <unistd.h>
#include <cstdio>
#include <cstring>

#include "config.h"

extern "C" {
#include <libdisplay-info/info.h>
#include <libinput.h>
#include <libliftoff.h>
}

#include "cursor/xcursor.h"
#include "info/info.h"
#include "input/keyboard.h"
#include "input/pointer.h"
#include "input/seat.h"
#include "input/touch.h"
#include "logging/logging.h"
#include "plane/plane.h"

#endif  // INCLUDE_DRMPP_H_
