/*
* Copyright 2021 The Chromium OS Authors. All rights reserved.
 * Use of this source code is governed by a BSD-style license that can be
 * found in the LICENSE file.
 */
#include "bs_drm.h"

void *xmalloc(size_t size)
{
  void *ptr = malloc(size);
  if (!ptr && size != 0) {
    assert(0); // for the error message in debug builds
    abort();
  }
  return ptr;
}
void *xcalloc(size_t n, size_t size)
{
  void *ptr = calloc(n, size);
  if (!ptr && n != 0 && size != 0) {
    assert(0); // for the error message in debug builds
    abort();
  }
  return ptr;
}
void *xrealloc(void *ptr, size_t size)
{
  ptr = realloc(ptr, size);
  if (!ptr && size != 0) {
    assert(0); // for the error message in debug builds
    abort();
  }
  return ptr;
}
char *xstrdup(const char *s)
{
  char *t = strdup(s);
  if (!t) {
    assert(0); // for the error message in debug builds
    abort();
  }
  return t;
}