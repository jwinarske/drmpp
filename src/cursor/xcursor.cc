/*
 * Copyright © 2024 drm contributors
 * Copyright © 2002 Keith Packard
 *
 * Permission is hereby granted, free of charge, to any person obtaining
 * a copy of this software and associated documentation files (the
 * "Software"), to deal in the Software without restriction, including
 * without limitation the rights to use, copy, modify, merge, publish,
 * distribute, sublicense, and/or sell copies of the Software, and to
 * permit persons to whom the Software is furnished to do so, subject to
 * the following conditions:
 *
 * The above copyright notice and this permission notice (including the
 * next paragraph) shall be included in all copies or substantial
 * portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND,
 * EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF
 * MERCHANTABILITY, FITNESS FOR A PARTICULAR PURPOSE AND
 * NONINFRINGEMENT.  IN NO EVENT SHALL THE AUTHORS OR COPYRIGHT HOLDERS
 * BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER LIABILITY, WHETHER IN AN
 * ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM, OUT OF OR IN
 * CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include "cursor/xcursor.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <memory>
#include <vector>

namespace drmpp {
  static constexpr uint32_t kMagic = 0x72756358; // "Xcur" LSBFirst
  static constexpr uint32_t kFileMajor = 1;
  static constexpr uint32_t kFileMinor = 0;
  static constexpr uint32_t kFileVersion = ((kFileMajor << 16) | (kFileMinor));
  static constexpr uint32_t kFileHeaderLength = (4 * 4);
  static constexpr uint32_t kImageType = 0xfffd0002;
  static constexpr uint32_t kImageVersion = 1;
  static constexpr uint32_t kMaxCursorSize = 0x7fff; // 32767 x 32767

  XCursor::Image::Image(const int width, const int height)
    : version(kImageVersion),
      width(width),
      height(height),
      xhot(0),
      yhot(0),
      delay(0),
      pixels(width * height) {
  }

  XCursor::Images::Images(const int size) {
    (void) size;
  }

  XCursor::FileHeader::FileHeader(const uint32_t num_toc)
    : magic(kMagic),
      header(kFileHeaderLength),
      version(kFileVersion),
      num_toc(num_toc),
      toc(num_toc) {
  }

  std::unique_ptr<XCursor::Image> XCursor::create_image(uint32_t width,
                                                        uint32_t height) {
    // Check if width or height is zero or exceeds the maximum cursor size
    if (width == 0 || height == 0 || width > kMaxCursorSize ||
        height > kMaxCursorSize) {
      return nullptr;
    }
    // Create and return a new Image object
    return std::make_unique<Image>(width, height);
  }

  void XCursor::destroy_image(std::unique_ptr<Image> &image) {
    image.reset();
  }

  std::unique_ptr<XCursor::Images> XCursor::create_images(size_t size) {
    return std::make_unique<Images>(size);
  }

  void XCursor::destroy_images(std::unique_ptr<Images> &images) {
    images.reset();
  }

  bool XCursor::read_uint(std::ifstream &file, uint32_t &u) {
    if (unsigned char bytes[4]; file.read(reinterpret_cast<char *>(bytes), 4)) {
      u = static_cast<uint32_t>(bytes[0]) << 0 |
          static_cast<uint32_t>(bytes[1]) << 8 |
          static_cast<uint32_t>(bytes[2]) << 16 |
          static_cast<uint32_t>(bytes[3]) << 24;
      return true;
    }
    return false;
  }

  std::unique_ptr<XCursor::FileHeader> XCursor::read_file_header(
    std::ifstream &file) {
    FileHeader head(0);
    if (!read_uint(file, head.magic) || head.magic != kMagic ||
        !read_uint(file, head.header) || !read_uint(file, head.version) ||
        !read_uint(file, head.num_toc)) {
      return nullptr;
    }

    if (const uint32_t skip = head.header - kFileHeaderLength;
      skip && !file.seekg(skip, std::ios::cur)) {
      return nullptr;
    }

    auto file_header = std::make_unique<FileHeader>(head.num_toc);
    file_header->magic = head.magic;
    file_header->header = head.header;
    file_header->version = head.version;
    file_header->num_toc = head.num_toc;

    for (uint32_t n = 0; n < file_header->num_toc; ++n) {
      if (!read_uint(file, file_header->toc[n].type) ||
          !read_uint(file, file_header->toc[n].subtype) ||
          !read_uint(file, file_header->toc[n].position)) {
        return nullptr;
      }
    }
    return file_header;
  }

  bool XCursor::seek_to_toc(std::ifstream &file,
                            const FileHeader &file_header,
                            const int toc) {
    return file.seekg(file_header.toc[toc].position, std::ios::beg).good();
  }

  bool XCursor::read_chunk_header(std::ifstream &file,
                                  const FileHeader &file_header,
                                  const int toc,
                                  ChunkHeader &chunk_header) {
    if (!seek_to_toc(file, file_header, toc) ||
        !read_uint(file, chunk_header.header) ||
        !read_uint(file, chunk_header.type) ||
        !read_uint(file, chunk_header.subtype) ||
        !read_uint(file, chunk_header.version)) {
      return false;
    }
    return chunk_header.type == file_header.toc[toc].type &&
           chunk_header.subtype == file_header.toc[toc].subtype;
  }

  uint32_t XCursor::dist(const uint32_t a, const uint32_t b) {
    return a > b ? a - b : b - a;
  }

  uint32_t XCursor::best_size(const FileHeader &file_header,
                              const uint32_t size,
                              size_t &num_sizes) {
    uint32_t best_size_ = 0;
    num_sizes = 0;

    for (const auto &toc: file_header.toc) {
      if (toc.type == kImageType) {
        if (const uint32_t this_size = toc.subtype;
          !best_size_ || dist(this_size, size) < dist(best_size_, size)) {
          best_size_ = this_size;
          num_sizes = 1;
        } else if (this_size == best_size_) {
          num_sizes++;
        }
      }
    }
    return best_size_;
  }

  int XCursor::find_image_toc(const FileHeader &file_header,
                              const uint32_t size,
                              size_t count) {
    for (int toc = 0; static_cast<uint32_t>(toc) < file_header.num_toc; ++toc) {
      if (file_header.toc[toc].type == kImageType &&
          file_header.toc[toc].subtype == size) {
        if (count == 0) {
          return toc;
        }
        count--;
      }
    }
    return -1;
  }

  std::unique_ptr<XCursor::Image> XCursor::read_image(
    std::ifstream &file,
    const FileHeader &file_header,
    const int toc) {
    ChunkHeader chunk_header{};
    ImageHeader head{};

    if (!read_chunk_header(file, file_header, toc, chunk_header) ||
        !read_uint(file, head.width) || !read_uint(file, head.height) ||
        !read_uint(file, head.x_hot) || !read_uint(file, head.y_hot) ||
        !read_uint(file, head.delay)) {
      return nullptr;
    }

    if (head.width > kMaxCursorSize || head.height > kMaxCursorSize ||
        head.width == 0 || head.height == 0 || head.x_hot > head.width ||
        head.y_hot > head.height) {
      return nullptr;
    }

    auto image =
        create_image(static_cast<int>(head.width), static_cast<int>(head.height));
    if (!image) {
      return nullptr;
    }

    if (chunk_header.version < image->version) {
      image->version = chunk_header.version;
    }

    image->width = head.width;
    image->height = head.height;
    image->xhot = head.x_hot;
    image->yhot = head.y_hot;
    image->delay = head.delay;

    for (uint32_t &pixel: image->pixels) {
      if (!read_uint(file, pixel)) {
        destroy_image(image);
        return nullptr;
      }
    }
    return image;
  }

  std::unique_ptr<XCursor::Images> XCursor::load_images(std::ifstream &file,
                                                        const int size) {
    const auto file_header = read_file_header(file);
    if (!file_header) {
      return nullptr;
    }

    size_t n_size;
    const uint32_t best_size_ =
        best_size(*file_header, static_cast<uint32_t>(size), n_size);
    if (!best_size_) {
      return nullptr;
    }

    auto images = create_images(n_size);
    if (!images) {
      return nullptr;
    }

    for (size_t n = 0; n < n_size; ++n) {
      const int toc = find_image_toc(*file_header, best_size_, n);
      if (toc < 0) {
        break;
      }

      images->images.emplace_back(read_image(file, *file_header, toc));
      if (!images->images[n]) {
        break;
      }
    }

    if (images->images.size() != n_size) {
      destroy_images(images);
      return nullptr;
    }
    return images;
  }

  bool XCursor::read_uint(std::istream &file, uint32_t &u) {
    if (unsigned char bytes[4]; file.read(reinterpret_cast<char *>(bytes), 4)) {
      u = static_cast<uint32_t>(bytes[0]) << 0 |
          static_cast<uint32_t>(bytes[1]) << 8 |
          static_cast<uint32_t>(bytes[2]) << 16 |
          static_cast<uint32_t>(bytes[3]) << 24;
      return true;
    }
    return false;
  }

  std::unique_ptr<XCursor::FileHeader> XCursor::read_file_header(
    std::istream &file) {
    FileHeader head(0);
    if (!read_uint(file, head.magic) || head.magic != kMagic ||
        !read_uint(file, head.header) || !read_uint(file, head.version) ||
        !read_uint(file, head.num_toc)) {
      return nullptr;
    }

    if (const uint32_t skip = head.header - kFileHeaderLength;
      skip && !file.seekg(skip, std::ios::cur)) {
      return nullptr;
    }

    auto file_header = std::make_unique<FileHeader>(head.num_toc);
    file_header->magic = head.magic;
    file_header->header = head.header;
    file_header->version = head.version;
    file_header->num_toc = head.num_toc;

    for (uint32_t n = 0; n < file_header->num_toc; ++n) {
      if (!read_uint(file, file_header->toc[n].type) ||
          !read_uint(file, file_header->toc[n].subtype) ||
          !read_uint(file, file_header->toc[n].position)) {
        return nullptr;
      }
    }
    return file_header;
  }

  bool XCursor::seek_to_toc(std::istream &file,
                            const FileHeader &file_header,
                            const int toc) {
    return file.seekg(file_header.toc[toc].position, std::ios::beg).good();
  }

  bool XCursor::read_chunk_header(std::istream &file,
                                  const FileHeader &file_header,
                                  const int toc,
                                  ChunkHeader &chunk_header) {
    if (!seek_to_toc(file, file_header, toc) ||
        !read_uint(file, chunk_header.header) ||
        !read_uint(file, chunk_header.type) ||
        !read_uint(file, chunk_header.subtype) ||
        !read_uint(file, chunk_header.version)) {
      return false;
    }
    return chunk_header.type == file_header.toc[toc].type &&
           chunk_header.subtype == file_header.toc[toc].subtype;
  }

  std::unique_ptr<XCursor::Image> XCursor::read_image(
    std::istream &file,
    const FileHeader &file_header,
    const int toc) {
    ChunkHeader chunk_header{};
    ImageHeader head{};

    if (!read_chunk_header(file, file_header, toc, chunk_header) ||
        !read_uint(file, head.width) || !read_uint(file, head.height) ||
        !read_uint(file, head.x_hot) || !read_uint(file, head.y_hot) ||
        !read_uint(file, head.delay)) {
      return nullptr;
    }

    if (head.width > kMaxCursorSize || head.height > kMaxCursorSize ||
        head.width == 0 || head.height == 0 || head.x_hot > head.width ||
        head.y_hot > head.height) {
      return nullptr;
    }

    auto image =
        create_image(static_cast<int>(head.width), static_cast<int>(head.height));
    if (!image) {
      return nullptr;
    }

    if (chunk_header.version < image->version) {
      image->version = chunk_header.version;
    }

    image->width = head.width;
    image->height = head.height;
    image->xhot = head.x_hot;
    image->yhot = head.y_hot;
    image->delay = head.delay;

    for (uint32_t &pixel: image->pixels) {
      if (!read_uint(file, pixel)) {
        destroy_image(image);
        return nullptr;
      }
    }
    return image;
  }

  std::unique_ptr<XCursor::Images> XCursor::load_images(
    const std::vector<uint8_t> &buffer,
    const uint32_t size) {
    std::istringstream file(std::string(buffer.begin(), buffer.end()),
                            std::ios::binary);
    const auto file_header = read_file_header(file);
    if (!file_header) {
      return nullptr;
    }

    size_t n_size;
    const uint32_t best_size_ = best_size(*file_header, size, n_size);
    if (!best_size_) {
      return nullptr;
    }

    auto images = create_images(n_size);
    if (!images) {
      return nullptr;
    }

    for (size_t n = 0; n < n_size; ++n) {
      const int toc = find_image_toc(*file_header, best_size_, n);
      if (toc < 0) {
        break;
      }

      images->images.emplace_back(read_image(file, *file_header, toc));
      if (!images->images[n]) {
        break;
      }
    }

    if (images->images.size() != n_size) {
      destroy_images(images);
      return nullptr;
    }
    return images;
  }
} // namespace drmpp
