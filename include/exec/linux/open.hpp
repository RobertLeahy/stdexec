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
#include <linux/openat2.h>
#include <sys/stat.h>

namespace exec {

namespace detail::open {

template<typename Populate>
inline ::stdexec::sender auto impl(
  ::exec::io_uring_context& ctx,
  Populate populate,
  const char* const path,
  const ::open_how how) noexcept
{
  return
    ::stdexec::just(how) |
    ::stdexec::let_value([&ctx, populate, path](::open_how& stored_how) noexcept {
      return
        exec::io_or_throw(
          ctx,
          "IORING_OP_OPENAT2",
          [populate, path, &stored_how](::io_uring_sqe& sqe) noexcept {
            std::memset(&sqe, 0, sizeof(sqe));
            sqe.opcode = IORING_OP_OPENAT2;
            populate(sqe);
            sqe.addr = reinterpret_cast<std::uintptr_t>(path);
            sqe.len = static_cast<std::uint32_t>(sizeof(stored_how));
            sqe.off = reinterpret_cast<std::uintptr_t>(&stored_how);
          }) |
        ::stdexec::then([](const ::io_uring_cqe& cqe) noexcept {
          return cqe.res;
        });
    });
}

} // namespace detail::open

inline ::stdexec::sender auto open(
  io_uring_context& ctx,
  const int dirfd,
  const char* const path,
  const int flags,
  const ::mode_t mode = 0) noexcept
{
  const ::open_how how{
    .flags = static_cast<std::uint64_t>(flags),
    .mode = static_cast<std::uint64_t>(mode),
    .resolve = 0
  };
  return detail::open::impl(
    ctx,
    [dirfd](::io_uring_sqe& sqe) noexcept { sqe.fd = dirfd; },
    path,
    how);
}

inline ::stdexec::sender auto open(
  io_uring_context& ctx,
  const int dirfd,
  const char* const path,
  const ::open_how how) noexcept
{
  return detail::open::impl(
    ctx,
    [dirfd](::io_uring_sqe& sqe) noexcept { sqe.fd = dirfd; },
    path,
    how);
}

inline ::stdexec::sender auto open(
  io_uring_context& ctx,
  const char* const path,
  const int flags,
  const ::mode_t mode = 0) noexcept
{
  return open(ctx, AT_FDCWD, path, flags, mode);
}

inline ::stdexec::sender auto open(
  io_uring_context& ctx,
  const char* const path,
  const ::open_how how) noexcept
{
  return open(ctx, AT_FDCWD, path, how);
}

template<can_populate_sqe FD>
::stdexec::sender auto open(
  FD& fd,
  const char* const path,
  const int flags,
  const ::mode_t mode = 0) noexcept
{
  const ::open_how how{
    .flags = static_cast<std::uint64_t>(flags),
    .mode = static_cast<std::uint64_t>(mode),
    .resolve = 0
  };
  return detail::open::impl(
    fd.context(),
    [&fd](::io_uring_sqe& sqe) noexcept { fd.populate_sqe(sqe); },
    path,
    how);
}

template<can_populate_sqe FD>
::stdexec::sender auto open(
  FD& fd,
  const char* const path,
  const ::open_how how) noexcept
{
  return detail::open::impl(
    fd.context(),
    [&fd](::io_uring_sqe& sqe) noexcept { fd.populate_sqe(sqe); },
    path,
    how);
}

} // namespace exec
