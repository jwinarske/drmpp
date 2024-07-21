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

#ifndef INCLUDE_DRMPP_KMS_INFO_INFO_H_
#define INCLUDE_DRMPP_KMS_INFO_INFO_H_

#include <rapidjson/document.h>


namespace drmpp::info {
    struct DrmInfo {
        static std::string get_node_info(const char *path);

    private:
        static unsigned long tainted_info();

        [[nodiscard]] static rapidjson::Value kernel_info(rapidjson::MemoryPoolAllocator<> &allocator);

        [[nodiscard]] static rapidjson::Value device_info(int fd, rapidjson::MemoryPoolAllocator<> &allocator);

        [[nodiscard]] static rapidjson::Value driver_info(int fd, rapidjson::MemoryPoolAllocator<> &allocator);

        [[nodiscard]] static rapidjson::Value in_formats_info(int fd, uint32_t blob_id,
                                                              rapidjson::MemoryPoolAllocator<> &allocator);

        static rapidjson::Value mode_info(const drmModeModeInfo *mode,
                                          rapidjson::MemoryPoolAllocator<> &allocator);

        [[nodiscard]] static rapidjson::Value mode_id_info(int fd, uint32_t blob_id,
                                                           rapidjson::MemoryPoolAllocator<> &allocator);

        [[nodiscard]] static rapidjson::Value writeback_pixel_formats_info(int fd, uint32_t blob_id,
                                                                           rapidjson::MemoryPoolAllocator<> &allocator);

        static rapidjson::Value path_info(int fd, uint32_t blob_id);

        [[nodiscard]] static rapidjson::Value hdr_output_metadata_info(int fd, uint32_t blob_id,
                                                                       rapidjson::MemoryPoolAllocator<> &allocator);

        [[nodiscard]] static rapidjson::Value fb_info(int fd, uint32_t id,
                                                      rapidjson::MemoryPoolAllocator<> &allocator);

        static rapidjson::Value properties_info(int fd, uint32_t id, uint32_t type,
                                                rapidjson::MemoryPoolAllocator<> &allocator);

        static rapidjson::Value connectors_info(int fd, const drmModeRes *res,
                                                rapidjson::MemoryPoolAllocator<> &allocator);

        static rapidjson::Value encoders_info(int fd, const drmModeRes *res,
                                              rapidjson::MemoryPoolAllocator<> &allocator);

        static rapidjson::Value crtcs_info(int fd, const drmModeRes *res, rapidjson::MemoryPoolAllocator<> &allocator);

        static rapidjson::Value planes_info(int fd, rapidjson::MemoryPoolAllocator<> &allocator);

        static void node_info(const char *path, rapidjson::Document &d);
    };
}

#endif // INCLUDE_DRMPP_KMS_INFO_INFO_H_
