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
#include <utility>
#include "io_or_throw.hpp"
#include "io_uring_context.hpp"
#include "io_uring_file_descriptor.hpp"
#include "../../stdexec/execution.hpp"

#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>

namespace exec {

struct io_uring_open : io_uring_file_descriptor {
  explicit io_uring_open(
    io_uring_context& ctx,
    const int dirfd,
    const char* const path,
    const int flags,
    const ::mode_t mode = 0) noexcept
    : io_uring_file_descriptor(ctx),
      dirfd_(dirfd),
      path_(path),
      how_{.flags = static_cast<std::uint64_t>(flags),
           .mode = static_cast<std::uint64_t>(mode),
           .resolve = 0}
  {}
  explicit io_uring_open(
    io_uring_context& ctx,
    const int dirfd,
    const char* const path,
    const ::open_how how) noexcept
    : io_uring_file_descriptor(ctx),
      dirfd_(dirfd),
      path_(path),
      how_(how)
  {}
  explicit io_uring_open(
    io_uring_context& ctx,
    const char* const path,
    const int flags,
    const ::mode_t mode = 0) noexcept
    : io_uring_open(ctx, AT_FDCWD, path, flags, mode)
  {}
  explicit io_uring_open(
    io_uring_context& ctx,
    const char* const path,
    const ::open_how how) noexcept
    : io_uring_open(ctx, AT_FDCWD, path, how)
  {}
  ::stdexec::sender auto construct(void* const ptr) noexcept {
    return
      exec::io_or_throw(
        ctx_,
        "IORING_OP_OPENAT2",
        [&](::io_uring_sqe& sqe) noexcept {
          std::memset(&sqe, 0, sizeof(sqe));
          sqe.opcode = IORING_OP_OPENAT2;
          sqe.fd = dirfd_;
          sqe.addr = reinterpret_cast<std::uintptr_t>(path_);
          sqe.len = static_cast<std::uint32_t>(sizeof(how_));
          sqe.off = reinterpret_cast<std::uintptr_t>(&how_);
        }) |
      ::stdexec::then([&, ptr](const ::io_uring_cqe& cqe) noexcept {
        new(ptr) type(ctx_, cqe.res);
      });
  }
private:
  int dirfd_;
  const char* path_;
  ::open_how how_;
};

} // namespace exec
