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
#include <new>
#include "io_or_throw.hpp"
#include "io_uring_context.hpp"
#include "io_uring_file_descriptor.hpp"
#include "../../stdexec/execution.hpp"

#include <sys/socket.h>

namespace exec {

struct io_uring_accept : io_uring_file_descriptor {
  struct type : io_uring_file_descriptor::type {
    constexpr explicit type(io_uring_context& ctx, const int fd, const ::sockaddr_storage& addr) noexcept
      : io_uring_file_descriptor::type(ctx, fd),
        addr_(addr)
    {}
    constexpr const ::sockaddr_storage& remote_address() const noexcept {
      return addr_;
    }
  private:
    ::sockaddr_storage addr_;
  };
private:
  struct scratch_ {
    ::sockaddr_storage addr{};
    ::socklen_t len{sizeof(addr)};
  };
  static_assert(alignof(type) >= alignof(scratch_));
  static_assert(sizeof(type) >= sizeof(scratch_));
public:
  explicit constexpr io_uring_accept(
    io_uring_file_descriptor::type& fd,
    const int flags = 0) noexcept
    : io_uring_file_descriptor(fd.context()),
      fd_(fd.native_handle()),
      flags_(flags)
  {}
  ::stdexec::sender auto construct(void* const ptr) noexcept {
    auto&& scratch = *new(ptr) scratch_;
    return
      exec::io_or_throw(
        ctx_,
        "IORING_OP_ACCEPT",
        [&](::io_uring_sqe& sqe) noexcept {
          std::memset(&sqe, 0, sizeof(sqe));
          sqe.opcode = IORING_OP_ACCEPT;
          sqe.fd = fd_;
          sqe.addr = reinterpret_cast<std::uintptr_t>(&scratch.addr);
          sqe.addr2 = reinterpret_cast<std::uintptr_t>(&scratch.len);
          sqe.accept_flags = flags_;
        }) |
      ::stdexec::then([&, ptr](const ::io_uring_cqe& cqe) noexcept {
        const auto local = scratch.addr;
        new(ptr) type(ctx_, cqe.res, local);
      });
  }
private:
  int fd_;
  int flags_;
};

} // namespace exec
