/**
 * SPDX-FileCopyrightText: Copyright (c) 2021-2022, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include "srf/codable/api.hpp"
#include "srf/codable/type_traits.hpp"
#include "srf/utils/sfinae_concept.hpp"

#include <memory>

namespace srf::codable {

template <typename T>
struct Decoder final
{
  public:
    Decoder(const IDecodableStorage& storage) : m_storage(storage) {}

    T deserialize(std::size_t object_idx) const
    {
        return detail::deserialize<T>(sfinae::full_concept{}, *this, object_idx);
    }

  protected:
    void copy_from_buffer(const idx_t& idx, memory::buffer_view dst_view) const
    {
        m_storage.copy_from_buffer(idx, std::move(dst_view));
    }

    std::size_t buffer_size(const idx_t& idx) const
    {
        return m_storage.buffer_size(idx);
    }

  private:
    const IDecodableStorage& m_storage;

    friend T;
    friend codable_protocol<T>;
};

template <typename T>
auto decode(const IDecodableStorage& encoded, std::size_t object_idx = 0)
{
    Decoder<T> decoder(encoded);
    return decoder.deserialize(object_idx);
}

}  // namespace srf::codable
