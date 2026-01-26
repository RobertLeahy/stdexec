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

namespace exec {

namespace detail::unlink {

template<typename Populate>
::STDEXEC::sender auto impl(
  exec::io_uring_context& ctx,
  Populate populate,
  const char* const path,
  const std::uint32_t flags) noexcept
{
  return
    exec::io_or_throw(
      ctx,
      "IORING_OP_UNLINKAT",
      [populate, path, flags](::io_uring_sqe& sqe) noexcept {
          std::memset(&sqe, 0, sizeof(sqe));
          sqe.opcode = IORING_OP_UNLINKAT;
          populate(sqe);
          sqe.addr = reinterpret_cast<std::uintptr_t>(path);
          sqe.unlink_flags = flags;
      }) |
    ::STDEXEC::then([](const ::io_uring_cqe&) noexcept {
      //  The CQE conveys no information beyond success or failure which
      //  io_or_throw handles.
    });
}

} // namespace detail::unlink

inline ::STDEXEC::sender auto unlink(
  io_uring_context& ctx,
  const int dirfd,
  const char* const path,
  const std::uint32_t flags = 0) noexcept
{
  return detail::unlink::impl(
    ctx,
    [dirfd](::io_uring_sqe& sqe) noexcept { sqe.fd = dirfd; },
    path,
    flags);
}

template<can_populate_sqe FD>
::STDEXEC::sender auto unlink(
  FD& fd,
  const char* const path,
  const std::uint32_t flags = 0) noexcept
{
  return detail::unlink::impl(
    fd.context(),
    [&fd](::io_uring_sqe& sqe) noexcept { fd.populate_sqe(sqe); },
    path,
    flags);
}

inline ::STDEXEC::sender auto unlink(
  io_uring_context& ctx,
  const char* const path,
  const std::uint32_t flags = 0) noexcept
{
  return unlink(ctx, AT_FDCWD, path, flags);
}

} // namespace exec
