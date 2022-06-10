// Copyright 2022 Espressif Systems (Shanghai) PTE LTD
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at

//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#pragma once

namespace esp_modem {
/**
 * DTE buffer
 */
struct unique_buffer {
    explicit unique_buffer(size_t size);
    explicit unique_buffer(): data(nullptr), size(0), consumed(0) { }
    unique_buffer(unique_buffer&& other) noexcept
    {
        data = std::move(other.data);
        size = other.size;
        consumed = 0;
    }
    std::unique_ptr<uint8_t[]> data;
    size_t size{};
    size_t consumed{};
    unique_buffer& operator=(unique_buffer&& other) noexcept
    {
        return *this;
    }
    [[nodiscard]] uint8_t *get() const;
};

}