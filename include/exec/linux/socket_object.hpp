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

#include "file_descriptor.hpp"
#include "io_uring_context.hpp"
#include "socket.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

struct socket_object {
  using type = file_descriptor;

  constexpr explicit socket_object(
    io_uring_context& ctx,
    const int domain,
    const int type,
    const int protocol) noexcept
    : ctx_(ctx),
      domain_(domain),
      type_(type),
      protocol_(protocol)
  {}

  ::STDEXEC::sender auto operator()(type* storage) const noexcept {
    return
      ::exec::socket(ctx_, domain_, type_, protocol_) |
      ::STDEXEC::then([&ctx = ctx_, storage](const int fd) noexcept {
        auto* ptr = new(storage) type(ctx, fd);
        return ptr->close();
      });
  }
private:
  io_uring_context& ctx_;
  int domain_;
  int type_;
  int protocol_;
};

} // namespace exec
