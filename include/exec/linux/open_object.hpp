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

#include <utility>
#include "file_descriptor.hpp"
#include "io_uring_context.hpp"
#include "open.hpp"
#include "../../stdexec/execution.hpp"

#include <fcntl.h>
#include <linux/openat2.h>
#include <sys/stat.h>

namespace exec {

struct open_object {
  using type = file_descriptor;

  explicit open_object(
    io_uring_context& ctx,
    const int dirfd,
    const char* const path,
    const int flags,
    const ::mode_t mode = 0) noexcept
    : ctx_(ctx),
      dirfd_(dirfd),
      path_(path),
      how_{.flags = static_cast<std::uint64_t>(flags),
           .mode = static_cast<std::uint64_t>(mode),
           .resolve = 0}
  {}

  explicit open_object(
    io_uring_context& ctx,
    const int dirfd,
    const char* const path,
    const ::open_how how) noexcept
    : ctx_(ctx),
      dirfd_(dirfd),
      path_(path),
      how_(how)
  {}

  explicit open_object(
    io_uring_context& ctx,
    const char* const path,
    const int flags,
    const ::mode_t mode = 0) noexcept
    : open_object(ctx, AT_FDCWD, path, flags, mode)
  {}

  explicit open_object(
    io_uring_context& ctx,
    const char* const path,
    const ::open_how how) noexcept
    : open_object(ctx, AT_FDCWD, path, how)
  {}

  ::exec::enter_sender auto operator()(type* storage) const noexcept {
    return
      ::exec::open(ctx_, dirfd_, path_, how_) |
      ::stdexec::then([&ctx = ctx_, storage](const int fd) noexcept {
        auto* ptr = new(storage) type(ctx, fd);
        return ptr->close();
      });
  }
private:
  io_uring_context& ctx_;
  int dirfd_;
  const char* path_;
  ::open_how how_;
};

} // namespace exec
