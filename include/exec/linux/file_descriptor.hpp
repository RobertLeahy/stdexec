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

#include <type_traits>
#include <utility>
#include "close.hpp"
#include "has_file_descriptor.hpp"
#include "io_uring_context.hpp"
#include "../variant_sender.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

struct file_descriptor {
  using native_handle_type = int;
  constexpr explicit file_descriptor(io_uring_context& ctx, const int fd) noexcept
    : ctx_(ctx),
      fd_(fd)
  {}

  file_descriptor(const file_descriptor&) = delete;
  file_descriptor& operator=(const file_descriptor&) = delete;

  constexpr native_handle_type native_handle() noexcept {
    return fd_;
  }
  constexpr io_uring_context& context() const noexcept {
    return ctx_;
  }
  constexpr void populate_sqe(::io_uring_sqe& sqe) noexcept {
    sqe.fd = native_handle();
  }
  ::STDEXEC::sender auto close() noexcept {
    auto close_sender =
      ::exec::close(*this) |
      ::STDEXEC::then([this]() noexcept { fd_ = -1; });
    using just_sender_t = decltype(::STDEXEC::just());
    using close_sender_t = decltype(close_sender);
    if (fd_ < 0) {
      return ::exec::variant_sender<just_sender_t, close_sender_t>(
        ::STDEXEC::just());
    }
    return ::exec::variant_sender<just_sender_t, close_sender_t>(
      std::move(close_sender));
  }
  constexpr int release() noexcept {
    return std::exchange(fd_, -1);
  }
private:
  io_uring_context& ctx_;
  int fd_;
};

static_assert(std::is_trivially_destructible_v<file_descriptor>);

} // namespace exec
