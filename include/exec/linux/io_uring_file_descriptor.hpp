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

#include <cstring>
#include "io_uring_context.hpp"
#include "../env.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

struct io_uring_file_descriptor {
  struct type {
    constexpr explicit type(io_uring_context& ctx, const int fd) noexcept
      : ctx_(ctx),
        fd_(fd)
    {}
    type(const type&) = delete;
    type& operator=(const type&) = delete;
    using native_handle_type = int;
    constexpr native_handle_type native_handle() noexcept {
      return fd_;
    }
    constexpr void populate_sqe(::io_uring_sqe& sqe) noexcept {
      sqe.fd = native_handle();
    }
    constexpr io_uring_context& context() const noexcept {
      return ctx_;
    }
  private:
    io_uring_context& ctx_;
    int fd_;
  };
  constexpr explicit io_uring_file_descriptor(io_uring_context& ctx) noexcept
    : ctx_(ctx)
  {}
  auto destroy(type* const ptr) noexcept {
    return
      ctx_.io([ptr](::io_uring_sqe& sqe) noexcept {
        std::memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_CLOSE;
        sqe.fd = ptr->native_handle();
      }) | ::stdexec::then([](const ::io_uring_cqe& cqe) noexcept {
        STDEXEC_ASSERT(!cqe.res);
        (void)cqe;
      }) | ::stdexec::write_env(
        ::stdexec::prop(
          ::stdexec::get_stop_token,
          ::stdexec::never_stop_token{}));
  }
protected:
  io_uring_context& ctx_;
};

} // namespace exec
