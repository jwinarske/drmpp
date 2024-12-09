/*
  FastLZ - Byte-aligned LZ77 compression library
  Copyright (C) 2005-2020 Ariya Hidayat <ariya.hidayat@gmail.com>

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
  OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
  THE SOFTWARE.
*/

#include "input/fastlz.h"

#include <cstdint>
#include <cstring>

#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wimplicit-fallthrough"

//
// Give hints to the compiler for branch prediction optimization.
//
#if defined(__clang__) || (defined(__GNUC__) && (__GNUC__ > 2))
#define FASTLZ_LIKELY(c) (__builtin_expect(!!(c), 1))
#define FASTLZ_UNLIKELY(c) (__builtin_expect(!!(c), 0))
#else
#define FASTLZ_LIKELY(c) (c)
#define FASTLZ_UNLIKELY(c) (c)
#endif

//
// Specialize custom 64-bit implementation for speed improvements.
//
#if defined(__x86_64__) || defined(_M_X64) || defined(__aarch64__)
#define FLZ_ARCH64
#endif

//
// Workaround for DJGPP to find uint8_t, uint16_t, etc.
//
#if defined(__MSDOS__) && defined(__GNUC__)
#include <stdint-gcc.h>
#endif

#if defined(FASTLZ_USE_MEMMOVE) && (FASTLZ_USE_MEMMOVE == 0)

static void fastlz_memmove(uint8_t* dest,
                           const uint8_t* src,
                           std::size_t count) {
  do {
    *dest++ = *src++;
  } while (--count);
}

static void fastlz_memcpy(uint8_t* dest,
                          const uint8_t* src,
                          std::size_t count) {
  return fastlz_memmove(dest, src, count);
}

#else

static void fastlz_memmove(uint8_t* dest,
                           const uint8_t* src,
                           std::size_t count) {
  if (count > 4 && dest >= src + count) {
    std::memmove(dest, src, count);
  } else {
    switch (count) {
      default:
        do {
          *dest++ = *src++;
        } while (--count);
        break;
      case 3:  // NOLINT
        *dest++ = *src++;
      case 2:
        *dest++ = *src++;
      case 1:
        *dest++ = *src++;  // NOLINT
      case 0:
        break;
    }
  }
}

static void fastlz_memcpy(uint8_t* dest,
                          const uint8_t* src,
                          std::size_t count) {
  std::memcpy(dest, src, count);
}

#endif

#if defined(FLZ_ARCH64)

static uint32_t flz_readu32(const void* ptr) {
  return *static_cast<const uint32_t*>(ptr);
}

static uint32_t flz_cmp(const uint8_t* p, const uint8_t* q, const uint8_t* r) {
  const uint8_t* start = p;

  if (flz_readu32(p) == flz_readu32(q)) {
    p += 4;
    q += 4;
  }
  while (q < r)
    if (*p++ != *q++)
      break;
  return p - start;
}

#endif  // FLZ_ARCH64

#if !defined(FLZ_ARCH64)

static uint32_t flz_readu32(const void* ptr) {
  const uint8_t* p = static_cast<const uint8_t*>(ptr);
  return (p[3] << 24) | (p[2] << 16) | (p[1] << 8) | p[0];
}

static uint32_t flz_cmp(const uint8_t* p, const uint8_t* q, const uint8_t* r) {
  const uint8_t* start = p;
  while (q < r)
    if (*p++ != *q++)
      break;
  return p - start;
}

#endif  // !FLZ_ARCH64

constexpr std::size_t MAX_COPY = 32;
constexpr std::size_t MAX_LEN = 264;  // 256 + 8
constexpr std::size_t MAX_L1_DISTANCE = 8192;
constexpr std::size_t MAX_L2_DISTANCE = 8191;
constexpr std::size_t MAX_FAR_DISTANCE = (65535 + MAX_L2_DISTANCE - 1);

constexpr std::size_t HASH_LOG = 13;
constexpr std::size_t HASH_SIZE = (1 << HASH_LOG);
constexpr std::size_t HASH_MASK = (HASH_SIZE - 1);

static uint16_t flz_hash(const uint32_t v) {
  const uint32_t h = (v * 2654435769LL) >> (32 - HASH_LOG);
  return h & HASH_MASK;
}

// special case of memcpy: at most MAX_COPY bytes
static void flz_small_copy(uint8_t* dest,
                           const uint8_t* src,
                           std::size_t count) {
#if defined(FLZ_ARCH64)
  if (count >= 4) {
    const auto* p = reinterpret_cast<const uint32_t*>(src);
    auto* q = reinterpret_cast<uint32_t*>(dest);
    while (count > 4) {
      *q++ = *p++;
      count -= 4;
      dest += 4;
      src += 4;
    }
  }
#endif
  fastlz_memcpy(dest, src, count);
}

// special case of memcpy: exactly MAX_COPY bytes
static void flz_max_copy(void* dest, const void* src) {
#if defined(FLZ_ARCH64)
  const auto* p = static_cast<const uint32_t*>(src);
  auto* q = static_cast<uint32_t*>(dest);
  *q++ = *p++;
  *q++ = *p++;
  *q++ = *p++;
  *q++ = *p++;
  *q++ = *p++;
  *q++ = *p++;
  *q++ = *p++;
  *q++ = *p++;  // NOLINT
#else
  fastlz_memcpy(static_cast<uint8_t*>(dest), static_cast<const uint8_t*>(src),
                MAX_COPY);
#endif
}

static uint8_t* flz_literals(std::size_t runs,
                             const uint8_t* src,
                             uint8_t* dest) {
  while (runs >= MAX_COPY) {
    *dest++ = MAX_COPY - 1;
    flz_max_copy(dest, src);
    src += MAX_COPY;
    dest += MAX_COPY;
    runs -= MAX_COPY;
  }
  if (runs > 0) {
    *dest++ = runs - 1;
    flz_small_copy(dest, src, runs);
    dest += runs;
  }
  return dest;
}

static uint8_t* flz1_match(std::size_t len, std::size_t distance, uint8_t* op) {
  --distance;
  if (FASTLZ_UNLIKELY(len > MAX_LEN - 2))
    while (len > MAX_LEN - 2) {
      *op++ = (7 << 5) + (distance >> 8);
      *op++ = MAX_LEN - 2 - 7 - 2;
      *op++ = distance & 255;
      len -= MAX_LEN - 2;
    }
  if (len < 7) {
    *op++ = (len << 5) + (distance >> 8);
    *op++ = distance & 255;
  } else {
    *op++ = (7 << 5) + (distance >> 8);
    *op++ = len - 7;
    *op++ = distance & 255;
  }
  return op;
}

#define FASTLZ_BOUND_CHECK(cond) \
  if (FASTLZ_UNLIKELY(!(cond)))  \
    return 0;

std::size_t fastlz1_compress(const void* input,
                             const std::size_t length,
                             void* output) {
  const auto* ip = static_cast<const uint8_t*>(input);
  const uint8_t* ip_start = ip;
  const uint8_t* ip_bound = ip + length - 4;  // because readU32
  const uint8_t* ip_limit = ip + length - 12 - 1;
  auto* op = static_cast<uint8_t*>(output);

  uint32_t htab[HASH_SIZE];
  uint32_t seq, hash;

  // initializes hash table
  for (hash = 0; hash < HASH_SIZE; ++hash)
    htab[hash] = 0;

  // we start with literal copy
  const uint8_t* anchor = ip;
  ip += 2;

  // main loop
  while (FASTLZ_LIKELY(ip < ip_limit)) {
    const uint8_t* ref;
    std::size_t distance, cmp;

    // find potential match
    do {
      seq = flz_readu32(ip) & 0xffffff;
      hash = flz_hash(seq);
      ref = ip_start + htab[hash];
      htab[hash] = ip - ip_start;
      distance = ip - ref;
      cmp = FASTLZ_LIKELY(distance < MAX_L1_DISTANCE)
                ? flz_readu32(ref) & 0xffffff
                : 0x1000000;
      if (FASTLZ_UNLIKELY(ip >= ip_limit))
        break;
      ++ip;
    } while (seq != cmp);

    if (FASTLZ_UNLIKELY(ip >= ip_limit))
      break;
    --ip;

    if (FASTLZ_LIKELY(ip > anchor)) {
      op = flz_literals(ip - anchor, anchor, op);
    }

    const std::size_t len = flz_cmp(ref + 3, ip + 3, ip_bound);
    op = flz1_match(len, distance, op);

    // update the hash at match boundary
    ip += len;
    seq = flz_readu32(ip);
    hash = flz_hash(seq & 0xffffff);
    htab[hash] = ip++ - ip_start;
    seq >>= 8;
    hash = flz_hash(seq);
    htab[hash] = ip++ - ip_start;

    anchor = ip;
  }

  const std::size_t copy = static_cast<const uint8_t*>(input) + length - anchor;
  op = flz_literals(copy, anchor, op);

  return static_cast<std::size_t>(op - static_cast<uint8_t*>(output));
}

std::size_t fastlz1_decompress(const void* input,
                               const std::size_t length,
                               void* output,
                               const std::size_t maxout) {
  const auto* ip = static_cast<const uint8_t*>(input);
  const uint8_t* ip_limit = ip + length;
  const uint8_t* ip_bound = ip_limit - 2;
  auto* op = static_cast<uint8_t*>(output);
  const uint8_t* op_limit = op + maxout;
  uint32_t ctrl = *ip++ & 31;

  while (true) {
    if (ctrl >= 32) {
      std::size_t len = (ctrl >> 5) - 1;
      const std::size_t ofs = (ctrl & 31) << 8;
      const uint8_t* ref = op - ofs - 1;
      if (len == 7 - 1) {
        FASTLZ_BOUND_CHECK(ip <= ip_bound);
        len += *ip++;
      }
      ref -= *ip++;
      len += 3;
      FASTLZ_BOUND_CHECK(op + len <= op_limit);
      FASTLZ_BOUND_CHECK(ref >= static_cast<uint8_t*>(output));
      fastlz_memmove(op, ref, len);
      op += len;
    } else {
      ctrl++;
      FASTLZ_BOUND_CHECK(op + ctrl <= op_limit);
      FASTLZ_BOUND_CHECK(ip + ctrl <= ip_limit);
      fastlz_memcpy(op, ip, ctrl);
      ip += ctrl;
      op += ctrl;
    }

    if (FASTLZ_UNLIKELY(ip > ip_bound))
      break;
    ctrl = *ip++;
  }

  return static_cast<std::size_t>(op - static_cast<uint8_t*>(output));
}

static uint8_t* flz2_match(std::size_t len, std::size_t distance, uint8_t* op) {
  --distance;
  if (distance < MAX_L2_DISTANCE) {
    if (len < 7) {
      *op++ = (len << 5) + (distance >> 8);
      *op++ = distance & 255;
    } else {
      *op++ = (7 << 5) + (distance >> 8);
      for (len -= 7; len >= 255; len -= 255)
        *op++ = 255;
      *op++ = len;
      *op++ = distance & 255;
    }
  } else {
    // far away, but not yet in the another galaxy...
    if (len < 7) {
      distance -= MAX_L2_DISTANCE;
      *op++ = (len << 5) + 31;
      *op++ = 255;
      *op++ = distance >> 8;
      *op++ = distance & 255;
    } else {
      distance -= MAX_L2_DISTANCE;
      *op++ = (7 << 5) + 31;
      for (len -= 7; len >= 255; len -= 255)
        *op++ = 255;
      *op++ = len;
      *op++ = 255;
      *op++ = distance >> 8;
      *op++ = distance & 255;
    }
  }
  return op;
}

std::size_t fastlz2_compress(const void* input,
                             const std::size_t length,
                             void* output) {
  const auto* ip = static_cast<const uint8_t*>(input);
  const uint8_t* ip_start = ip;
  const uint8_t* ip_bound = ip + length - 4;  // because readU32
  const uint8_t* ip_limit = ip + length - 12 - 1;
  auto* op = static_cast<uint8_t*>(output);

  uint32_t htab[HASH_SIZE];
  uint32_t seq, hash;

  // initializes hash table
  for (hash = 0; hash < HASH_SIZE; ++hash)
    htab[hash] = 0;

  // we start with literal copy
  const uint8_t* anchor = ip;
  ip += 2;

  // main loop
  while (FASTLZ_LIKELY(ip < ip_limit)) {
    const uint8_t* ref;
    std::size_t distance, cmp;

    // find potential match
    do {
      seq = flz_readu32(ip) & 0xffffff;
      hash = flz_hash(seq);
      ref = ip_start + htab[hash];
      htab[hash] = ip - ip_start;
      distance = ip - ref;
      cmp = FASTLZ_LIKELY(distance < MAX_FAR_DISTANCE)
                ? flz_readu32(ref) & 0xffffff
                : 0x1000000;
      if (FASTLZ_UNLIKELY(ip >= ip_limit))
        break;
      ++ip;
    } while (seq != cmp);

    if (FASTLZ_UNLIKELY(ip >= ip_limit))
      break;

    --ip;

    // far, needs at least 5-byte match
    if (distance >= MAX_L2_DISTANCE) {
      if (ref[3] != ip[3] || ref[4] != ip[4]) {
        ++ip;
        continue;
      }
    }

    if (FASTLZ_LIKELY(ip > anchor)) {
      op = flz_literals(ip - anchor, anchor, op);
    }

    const std::size_t len = flz_cmp(ref + 3, ip + 3, ip_bound);
    op = flz2_match(len, distance, op);

    // update the hash at match boundary
    ip += len;
    seq = flz_readu32(ip);
    hash = flz_hash(seq & 0xffffff);
    htab[hash] = ip++ - ip_start;
    seq >>= 8;
    hash = flz_hash(seq);
    htab[hash] = ip++ - ip_start;

    anchor = ip;
  }

  const std::size_t copy = static_cast<const uint8_t*>(input) + length - anchor;
  op = flz_literals(copy, anchor, op);

  // marker for fastlz2
  *static_cast<uint8_t*>(output) |= (1 << 5);

  return static_cast<std::size_t>(op - static_cast<uint8_t*>(output));
}

std::size_t fastlz2_decompress(const void* input,
                               const std::size_t length,
                               void* output,
                               const std::size_t maxout) {
  const auto* ip = static_cast<const std::byte*>(input);
  const std::byte* ip_limit = ip + length;
  const std::byte* ip_bound = ip_limit - 2;
  auto* op = static_cast<std::byte*>(output);
  const std::byte* op_limit = op + maxout;
  std::uint32_t ctrl = std::to_integer<std::uint32_t>(*ip++) & 31;

  while (true) {
    if (ctrl >= 32) {
      std::uint32_t len = (ctrl >> 5) - 1;
      std::uint32_t ofs = (ctrl & 31) << 8;
      const std::byte* ref = reinterpret_cast<const std::byte*>(op) - ofs - 1;

      std::uint8_t code;
      if (len == 7 - 1)
        do {
          FASTLZ_BOUND_CHECK(ip <= ip_bound);
          code = std::to_integer<std::uint8_t>(*ip++);
          len += code;
        } while (code == 255);
      code = std::to_integer<std::uint8_t>(*ip++);
      ref -= code;
      len += 3;

      // match from 16-bit distance
      if (FASTLZ_UNLIKELY(code == 255))
        if (FASTLZ_LIKELY(ofs == 31 << 8)) {
          FASTLZ_BOUND_CHECK(ip < ip_bound);
          ofs = std::to_integer<std::uint8_t>(*ip++) << 8;
          ofs += std::to_integer<std::uint8_t>(*ip++);
          ref = reinterpret_cast<const std::byte*>(op) - ofs - MAX_L2_DISTANCE -
                1;
        }

      FASTLZ_BOUND_CHECK(op + len <= op_limit);
      FASTLZ_BOUND_CHECK(ref >= static_cast<const std::byte*>(output));
      fastlz_memmove(reinterpret_cast<uint8_t*>(op),
                     reinterpret_cast<const uint8_t*>(ref), len);
      op += len;
    } else {
      ctrl++;
      FASTLZ_BOUND_CHECK(op + ctrl <= op_limit);
      FASTLZ_BOUND_CHECK(ip + ctrl <= ip_limit);
      fastlz_memcpy(reinterpret_cast<uint8_t*>(op),
                    reinterpret_cast<const uint8_t*>(ip), ctrl);
      ip += ctrl;
      op += ctrl;
    }

    if (FASTLZ_UNLIKELY(ip >= ip_limit))
      break;
    ctrl = std::to_integer<std::uint32_t>(*ip++);
  }

  return static_cast<std::size_t>(op - static_cast<std::byte*>(output));
}

std::size_t fastlz_compress(const void* input,
                            const std::size_t length,
                            void* output) {
  // for short block, choose fastlz1
  if (length < 65536)
    return fastlz1_compress(input, length, output);

  // else...
  return fastlz2_compress(input, length, output);
}

std::size_t fastlz_decompress(const void* input,
                              const std::size_t length,
                              void* output,
                              const std::size_t maxout) {
  // magic identifier for compression level
  const int level =
      (std::to_integer<std::uint8_t>(*static_cast<const std::byte*>(input)) >>
       5) +
      1;

  if (level == 1)
    return fastlz1_decompress(input, length, output, maxout);
  if (level == 2)
    return fastlz2_decompress(input, length, output, maxout);

  // unknown level, trigger error
  return 0;
}

std::size_t fastlz_compress_level(const int level,
                                  const void* input,
                                  const std::size_t length,
                                  void* output) {
  if (level == 1)
    return fastlz1_compress(input, length, output);
  if (level == 2)
    return fastlz2_compress(input, length, output);

  return 0;
}
