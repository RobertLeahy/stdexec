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

#include <fcntl.h>
#include <sys/stat.h>

namespace exec {

namespace detail::mkdir {

template<typename Populate>
::stdexec::sender auto impl(
  exec::io_uring_context& ctx,
  Populate populate,
  const char* const path,
  const ::mode_t mode) noexcept
{
  return
    exec::io_or_throw(
      ctx,
      "IORING_OP_MKDIRAT",
      [populate, path, mode](::io_uring_sqe& sqe) noexcept {
          std::memset(&sqe, 0, sizeof(sqe));
          sqe.opcode = IORING_OP_MKDIRAT;
          populate(sqe);
          sqe.addr = reinterpret_cast<std::uintptr_t>(path);
          sqe.len = static_cast<std::uint32_t>(mode);
      }) |
    ::stdexec::then([](const ::io_uring_cqe&) noexcept {
      //  The CQE conveys no information beyond success or failure which
      //  io_or_throw handles.
    });
}

} // namespace detail::mkdir

inline ::stdexec::sender auto mkdir(
  io_uring_context& ctx,
  const int dirfd,
  const char* const path,
  const ::mode_t mode = 0777) noexcept
{
  return detail::mkdir::impl(
    ctx,
    [dirfd](::io_uring_sqe& sqe) noexcept { sqe.fd = dirfd; },
    path,
    mode);
}

template<can_populate_sqe FD>
::stdexec::sender auto mkdir(
  FD& fd,
  const char* const path,
  const ::mode_t mode = 0777) noexcept
{
  return detail::mkdir::impl(
    fd.context(),
    [&fd](::io_uring_sqe& sqe) noexcept { fd.populate_sqe(sqe); },
    path,
    mode);
}

inline ::stdexec::sender auto mkdir(
  io_uring_context& ctx,
  const char* const path,
  const ::mode_t mode = 0777) noexcept
{
  return mkdir(ctx, AT_FDCWD, path, mode);
}

} // namespace exec
