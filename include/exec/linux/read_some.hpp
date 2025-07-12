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
#include <cstring>
#include <span>
#include "can_populate_sqe.hpp"
#include "io_or_throw.hpp"
#include "io_uring_context.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

template<can_populate_sqe FD>
::stdexec::sender auto read_some(
  FD& fd,
  const std::span<std::byte> buffer) noexcept
{
  return
    exec::io_or_throw(
      fd.context(),
      "IORING_OP_READ",
      [&fd, buffer](::io_uring_sqe& sqe) noexcept {
          std::memset(&sqe, 0, sizeof(sqe));
          sqe.opcode = IORING_OP_READ;
          fd.populate_sqe(sqe);
          sqe.addr = reinterpret_cast<std::uintptr_t>(buffer.data());
          sqe.len = buffer.size();
          sqe.off = -1;
      }) |
    ::stdexec::then([](const ::io_uring_cqe& cqe) noexcept {
      return std::size_t(cqe.res);
    });

}

} // namespace exec
