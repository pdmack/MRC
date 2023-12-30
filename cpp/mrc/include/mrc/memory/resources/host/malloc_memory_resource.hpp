/*
 * SPDX-FileCopyrightText: Copyright (c) 2021-2023, NVIDIA CORPORATION & AFFILIATES. All rights reserved.
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

#include "mrc/memory/resources/memory_resource.hpp"

#include <memory>

namespace mrc::memory {

class malloc_memory_resource final : public memory_resource
{
  public:
    static std::shared_ptr<malloc_memory_resource> instance()
    {
        static std::shared_ptr<malloc_memory_resource> instance = std::make_shared<malloc_memory_resource>();
        return instance;
    }

  private:
    void* do_allocate(std::size_t bytes) final
    {
        return std::malloc(bytes);
    }

    void do_deallocate(void* ptr, std::size_t /*__bytes*/) final
    {
        std::free(ptr);
    }

    memory_kind do_kind() const final
    {
        return memory_kind::host;
    }
};

}  // namespace mrc::memory
