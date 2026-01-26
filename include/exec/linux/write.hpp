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
#include <cstdint>
#include <span>
#include "can_populate_sqe.hpp"
#include "write_some.hpp"
#include "../repeat_effect_until.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

template<can_populate_sqe FD>
::STDEXEC::sender auto write(FD& fd, const std::span<const std::byte> buffer)
  noexcept
{
  return
    ::STDEXEC::just(buffer) |
    ::STDEXEC::let_value([&fd](std::span<const std::byte>& buffer) noexcept {
      return
        ::exec::repeat_effect_until(
          ::exec::write_some(fd, buffer) |
          ::STDEXEC::then([&buffer](const std::size_t bytes_written) noexcept {
            buffer = buffer.subspan(bytes_written);
            return buffer.empty();
          }));
    });
}

template<can_populate_sqe FD>
::STDEXEC::sender auto write(
  FD& fd,
  const std::span<const std::byte> buffer,
  const std::uint64_t offset) noexcept
{
  return
    ::STDEXEC::just(buffer, offset) |
    ::STDEXEC::let_value([&fd](
      std::span<const std::byte>& buffer,
      std::uint64_t& offset) noexcept {
      return
        ::exec::repeat_effect_until(
          ::exec::write_some(fd, buffer, offset) |
          ::STDEXEC::then([&buffer, &offset](
            const std::size_t bytes_written) noexcept {
            buffer = buffer.subspan(bytes_written);
            offset += bytes_written;
            return buffer.empty();
          }));
    });
}

} // namespace exec
