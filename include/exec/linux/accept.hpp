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

#include <cstdint>
#include <cstring>
#include "can_populate_sqe.hpp"
#include "io_or_throw.hpp"
#include "io_uring_context.hpp"
#include "../../stdexec/execution.hpp"

#include <sys/socket.h>

namespace exec {

template<can_populate_sqe FD>
::stdexec::sender auto accept(FD& fd, const int flags = 0) noexcept {
  return
    ::stdexec::just(::sockaddr_storage{}, ::socklen_t{sizeof(::sockaddr_storage)}) |
    ::stdexec::let_value([&fd, flags](::sockaddr_storage& addr, ::socklen_t& addr_len) noexcept {
      return
        exec::io_or_throw(
          fd.context(),
          "IORING_OP_ACCEPT",
          [&fd, flags, &addr, &addr_len](::io_uring_sqe& sqe) noexcept {
            std::memset(&sqe, 0, sizeof(sqe));
            sqe.opcode = IORING_OP_ACCEPT;
            fd.populate_sqe(sqe);
            sqe.addr = reinterpret_cast<std::uintptr_t>(&addr);
            sqe.addr2 = reinterpret_cast<std::uintptr_t>(&addr_len);
            sqe.accept_flags = static_cast<std::uint32_t>(flags);
          }) |
        ::stdexec::let_value([&addr](const ::io_uring_cqe& cqe) noexcept {
          return ::stdexec::just(cqe.res, addr);
        });
    });
}

} // namespace exec
