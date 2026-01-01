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

namespace exec {

namespace detail::sync {

template<::exec::can_populate_sqe FD>
::stdexec::sender auto impl(
  FD& fd,
  const std::uint64_t offset,
  const std::uint32_t length,
  const std::uint32_t flags) noexcept
{
  return
    exec::io_or_throw(
      fd.context(),
      "IORING_OP_FSYNC",
      [&fd, offset, length, flags](::io_uring_sqe& sqe) noexcept {
        std::memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_FSYNC;
        fd.populate_sqe(sqe);
        sqe.off = offset;
        sqe.len = length;
        sqe.fsync_flags = flags;
      }) |
    ::stdexec::then([](const ::io_uring_cqe&) noexcept {
      //  The CQE conveys no information beyond success or failure which
      //  io_or_throw handles.
    });
}

} // namespace detail::sync

template<can_populate_sqe FD>
::stdexec::sender auto sync(FD& fd) noexcept {
  return detail::sync::impl(fd, 0, 0, 0);
}

template<can_populate_sqe FD>
::stdexec::sender auto sync(
  FD& fd,
  const std::uint64_t offset,
  const std::uint32_t length) noexcept
{
  return detail::sync::impl(fd, offset, length, 0);
}

template<can_populate_sqe FD>
::stdexec::sender auto datasync(FD& fd) noexcept {
  return detail::sync::impl(fd, 0, 0, IORING_FSYNC_DATASYNC);
}

template<can_populate_sqe FD>
::stdexec::sender auto datasync(
  FD& fd,
  const std::uint64_t offset,
  const std::uint32_t length) noexcept
{
  return detail::sync::impl(fd, offset, length, IORING_FSYNC_DATASYNC);
}

} // namespace exec
