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

namespace detail::stat {

template<typename Populate>
inline ::STDEXEC::sender auto impl(
  exec::io_uring_context& ctx,
  Populate populate,
  const char* const path,
  const std::uint32_t mask,
  const std::uint32_t flags) noexcept
{
  using type = struct ::statx;
  return
    ::STDEXEC::just(type{}) |
    ::STDEXEC::let_value([&ctx, populate, path, mask, flags](type& out) noexcept {
      return
        exec::io_or_throw(
          ctx,
          "IORING_OP_STATX",
          [populate, path, flags, mask, &out](::io_uring_sqe& sqe) noexcept {
            std::memset(&sqe, 0, sizeof(sqe));
            sqe.opcode = IORING_OP_STATX;
            populate(sqe);
            sqe.addr = reinterpret_cast<std::uintptr_t>(path);
            sqe.statx_flags = flags;
            sqe.len = mask;
            sqe.addr2 = reinterpret_cast<std::uintptr_t>(&out);
          }) |
        ::STDEXEC::then([&out](const ::io_uring_cqe&) noexcept {
          return out;
        });
    });
}

} // namespace detail::stat

template<can_populate_sqe FD>
::STDEXEC::sender auto stat(
  FD& fd,
  const char* const path,
  const std::uint32_t mask = STATX_BASIC_STATS,
  const std::uint32_t flags = 0) noexcept
{
  return detail::stat::impl(
    fd.context(),
    [&fd](::io_uring_sqe& sqe) noexcept { fd.populate_sqe(sqe); },
    path,
    mask,
    flags);
}

template<can_populate_sqe FD>
::STDEXEC::sender auto stat(
  FD& fd,
  const std::uint32_t mask = STATX_BASIC_STATS,
  const std::uint32_t flags = 0) noexcept
{
  return detail::stat::impl(
    fd.context(),
    [&fd](::io_uring_sqe& sqe) noexcept { fd.populate_sqe(sqe); },
    "",
    mask,
    flags | AT_EMPTY_PATH);
}

inline ::STDEXEC::sender auto stat(
  io_uring_context& ctx,
  const int dirfd,
  const char* const path,
  const std::uint32_t mask = STATX_BASIC_STATS,
  const std::uint32_t flags = 0) noexcept
{
  return detail::stat::impl(
    ctx,
    [dirfd](::io_uring_sqe& sqe) noexcept { sqe.fd = dirfd; },
    path,
    mask,
    flags);
}

inline ::STDEXEC::sender auto stat(
  io_uring_context& ctx,
  const int fd,
  const std::uint32_t mask = STATX_BASIC_STATS,
  const std::uint32_t flags = 0) noexcept
{
  return detail::stat::impl(
    ctx,
    [fd](::io_uring_sqe& sqe) noexcept { sqe.fd = fd; },
    nullptr,
    mask,
    flags | AT_EMPTY_PATH);
}

inline ::STDEXEC::sender auto stat(
  io_uring_context& ctx,
  const char* const path,
  const std::uint32_t mask = STATX_BASIC_STATS,
  const std::uint32_t flags = 0) noexcept
{
  return detail::stat::impl(
    ctx,
    [](::io_uring_sqe& sqe) noexcept { sqe.fd = AT_FDCWD; },
    path,
    mask,
    flags);
}

} // namespace exec
