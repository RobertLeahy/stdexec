/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *                         Copyright (c) 2025 Robert Leahy. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Licensed under the Apache License, Version 2.0 with LLVM Exceptions (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstddef>
#include <span>
#include "can_populate_sqe.hpp"
#include "read_some.hpp"
#include "../repeat_effect_until.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

template<can_populate_sqe FD>
::stdexec::sender auto read(FD& fd, const std::span<std::byte> buffer)
  noexcept
{
  return
    ::stdexec::just(buffer) |
    ::stdexec::let_value([&fd](std::span<std::byte>& buffer) noexcept {
      return
        ::exec::repeat_effect_until(
          ::exec::read_some(fd, buffer) |
          ::stdexec::then([&buffer](const std::size_t bytes_written) noexcept {
            buffer = buffer.subspan(bytes_written);
            return buffer.empty();
          }));
    });
}

} // namespace exec
