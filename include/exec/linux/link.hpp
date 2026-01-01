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
#include "file_descriptor.hpp"
#include "io_or_throw.hpp"
#include "io_uring_context.hpp"
#include "../../stdexec/execution.hpp"

#include <fcntl.h>

namespace exec {

inline ::stdexec::sender auto link(
  io_uring_context& ctx,
  const int old_dirfd,
  const char* const old_path,
  const int new_dirfd,
  const char* const new_path,
  const std::uint32_t flags = 0) noexcept
{
  return
    exec::io_or_throw(
      ctx,
      "IORING_OP_LINKAT",
      [old_dirfd, old_path, new_dirfd, new_path, flags](
        ::io_uring_sqe& sqe) noexcept {
          std::memset(&sqe, 0, sizeof(sqe));
          sqe.opcode = IORING_OP_LINKAT;
          sqe.fd = old_dirfd;
          sqe.addr = reinterpret_cast<std::uintptr_t>(old_path);
          sqe.len = static_cast<std::uint32_t>(new_dirfd);
          sqe.addr2 = reinterpret_cast<std::uint64_t>(new_path);
          sqe.hardlink_flags = flags;
      }) |
    ::stdexec::then([](const ::io_uring_cqe&) noexcept {
      //  The CQE conveys no information beyond success or failure which
      //  io_or_throw handles.
    });
}

template<file_descriptor OldFD, file_descriptor NewFD>
::stdexec::sender auto link(
  OldFD& old_fd,
  const char* const old_path,
  NewFD& new_fd,
  const char* const new_path,
  const std::uint32_t flags = 0) noexcept
{
  return link(
    old_fd.context(),
    old_fd.native_handle(),
    old_path,
    new_fd.native_handle(),
    new_path,
    flags);
}

inline ::stdexec::sender auto link(
  io_uring_context& ctx,
  const char* const old_path,
  const char* const new_path,
  const std::uint32_t flags = 0) noexcept
{
  return link(ctx, AT_FDCWD, old_path, AT_FDCWD, new_path, flags);
}

} // namespace exec
