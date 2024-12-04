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

#ifndef INCLUDE_CURSOR_XCURSOR_H
#define INCLUDE_CURSOR_XCURSOR_H

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <memory>
#include <vector>

namespace drmpp {
/**
 * @class XCursor
 * @brief Class for handling X cursors.
 */
class XCursor {
 public:
  /**
   * @struct Image
   * @brief Represents an image in the cursor.
   */
  struct Image {
    uint32_t version;              ///< Version of the image
    uint32_t width;                ///< Width of the image
    uint32_t height;               ///< Height of the image
    uint32_t xhot;                 ///< X-coordinate of the hotspot
    uint32_t yhot;                 ///< Y-coordinate of the hotspot
    uint32_t delay;                ///< Delay of the image
    std::vector<uint32_t> pixels;  ///< Pixel data of the image

    /**
     * @brief Constructor for Image.
     * @param width Width of the image.
     * @param height Height of the image.
     */
    Image(int width, int height);
  };

  /**
   * @struct Images
   * @brief Represents a collection of images.
   */
  struct Images {
    std::vector<std::unique_ptr<Image>> images;  ///< Collection of images
    std::string name;  ///< Name associated with the images

    /**
     * @brief Constructor for Images.
     * @param size Number of images.
     */
    explicit Images(int size);
  };

  /**
   * @struct FileHeader
   * @brief Represents the file header of the cursor file.
   */
  struct FileHeader {
    /**
     * @struct TOC
     * @brief Represents an entry in the table of contents.
     */
    struct TOC {
      uint32_t type;      ///< Type of the entry
      uint32_t subtype;   ///< Subtype of the entry
      uint32_t position;  ///< Position of the entry
    };

    uint32_t magic;        ///< Magic number
    uint32_t header;       ///< Header length
    uint32_t version;      ///< Version number
    uint32_t num_toc;      ///< Number of table of contents entries
    std::vector<TOC> toc;  ///< Table of contents entries

    /**
     * @brief Constructor for FileHeader.
     * @param num_toc Number of table of contents entries.
     */
    explicit FileHeader(uint32_t num_toc);
  };

  /**
   * @struct ChunkHeader
   * @brief Represents the header of a chunk in the cursor file.
   */
  struct ChunkHeader {
    uint32_t header;
    uint32_t type;
    uint32_t subtype;
    uint32_t version;
  };

  /**
   * @struct ImageHeader
   * @brief Represents the header of an image in the cursor file.
   */
  struct ImageHeader {
    uint32_t width;
    uint32_t height;
    uint32_t x_hot;
    uint32_t y_hot;
    uint32_t delay;
  };

  /**
   * @brief Creates an image.
   * @param width Width of the image.
   * @param height Height of the image.
   * @return A unique pointer to the created image.
   */
  static std::unique_ptr<Image> create_image(uint32_t width, uint32_t height);

  /**
   * @brief Destroys an image.
   * @param image A unique pointer to the image to be destroyed.
   */
  static void destroy_image(std::unique_ptr<Image>& image);

  /**
   * @brief Creates a collection of images.
   * @param size Number of images.
   * @return A unique pointer to the created collection of images.
   */
  static std::unique_ptr<Images> create_images(size_t size);

  /**
   * @brief Destroys a collection of images.
   * @param images A unique pointer to the collection of images to be destroyed.
   */
  static void destroy_images(std::unique_ptr<Images>& images);

  /**
   * @brief Reads a 32-bit unsigned integer from a file.
   * @param file The input file stream.
   * @param u The variable to store the read value.
   * @return True if the read was successful, false otherwise.
   */
  static bool read_uint(std::ifstream& file, uint32_t& u);

  /**
   * @brief Reads the file header from a file.
   * @param file The input file stream.
   * @return A unique pointer to the read file header.
   */
  static std::unique_ptr<FileHeader> read_file_header(std::ifstream& file);

  /**
   * @brief Seeks to a table of contents entry in a file.
   * @param file The input file stream.
   * @param file_header The file header.
   * @param toc The table of contents entry index.
   * @return True if the seek was successful, false otherwise.
   */
  static bool seek_to_toc(std::ifstream& file,
                          const FileHeader& file_header,
                          int toc);

  /**
   * @brief Reads a chunk header from a file.
   * @param file The input file stream.
   * @param file_header The file header.
   * @param toc The table of contents entry index.
   * @param chunk_header The variable to store the read chunk header.
   * @return True if the read was successful, false otherwise.
   */
  static bool read_chunk_header(std::ifstream& file,
                                const FileHeader& file_header,
                                int toc,
                                struct ChunkHeader& chunk_header);

  /**
   * @brief Calculates the distance between two values.
   * @param a The first value.
   * @param b The second value.
   * @return The distance between the two values.
   */
  static uint32_t dist(uint32_t a, uint32_t b);

  /**
   * @brief Finds the best size for an image.
   * @param file_header The file header.
   * @param size The desired size.
   * @param num_sizes The number of sizes.
   * @return The best size.
   */
  static uint32_t best_size(const FileHeader& file_header,
                            uint32_t size,
                            size_t& num_sizes);

  /**
   * @brief Finds the table of contents entry for an image.
   * @param file_header The file header.
   * @param size The desired size.
   * @param count The count of entries.
   * @return The index of the table of contents entry.
   */
  static int find_image_toc(const FileHeader& file_header,
                            uint32_t size,
                            size_t count);

  /**
   * @brief Reads an image from a file.
   * @param file The input file stream.
   * @param file_header The file header.
   * @param toc The table of contents entry index.
   * @return A unique pointer to the read image.
   */
  static std::unique_ptr<Image> read_image(std::ifstream& file,
                                           const FileHeader& file_header,
                                           int toc);

  /**
   * @brief Loads images from a file.
   * @param file The input file stream.
   * @param size The desired size.
   * @return A unique pointer to the loaded images.
   */
  static std::unique_ptr<Images> load_images(std::ifstream& file,
                                             int size = 24);

  /**
   * @brief Reads a 32-bit unsigned integer from a stream.
   * @param file The input stream.
   * @param u The variable to store the read value.
   * @return True if the read was successful, false otherwise.
   */
  static bool read_uint(std::istream& file, uint32_t& u);

  /**
   * @brief Reads the file header from a stream.
   * @param file The input stream.
   * @return A unique pointer to the read file header.
   */
  static std::unique_ptr<FileHeader> read_file_header(std::istream& file);

  /**
   * @brief Seeks to a table of contents entry in a stream.
   * @param file The input stream.
   * @param file_header The file header.
   * @param toc The table of contents entry index.
   * @return True if the seek was successful, false otherwise.
   */
  static bool seek_to_toc(std::istream& file,
                          const FileHeader& file_header,
                          int toc);

  /**
   * @brief Reads a chunk header from a stream.
   * @param file The input stream.
   * @param file_header The file header.
   * @param toc The table of contents entry index.
   * @param chunk_header The variable to store the read chunk header.
   * @return True if the read was successful, false otherwise.
   */
  static bool read_chunk_header(std::istream& file,
                                const FileHeader& file_header,
                                int toc,
                                ChunkHeader& chunk_header);

  /**
   * @brief Reads an image from a stream.
   * @param file The input stream.
   * @param file_header The file header.
   * @param toc The table of contents entry index.
   * @return A unique pointer to the read image.
   */
  static std::unique_ptr<Image> read_image(std::istream& file,
                                           const FileHeader& file_header,
                                           int toc);

  /**
   * @brief Loads images from a buffer.
   * @param buffer The input buffer.
   * @param size The desired size.
   * @return A unique pointer to the loaded images.
   */
  static std::unique_ptr<Images> load_images(const std::vector<uint8_t>& buffer,
                                             uint32_t size = 24);
};
}  // namespace drmpp
#endif  // INCLUDE_CURSOR_XCURSOR_H
