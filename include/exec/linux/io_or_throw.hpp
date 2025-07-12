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

#include <functional>
#include "io_uring_context.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

template<typename Invocable>
  requires requires(io_uring_context ctx, Invocable i) {
    ctx.io((Invocable&&)i);
  }
auto io_or_throw(
  io_uring_context& ctx,
  const char* const syscall,
  Invocable i) noexcept(noexcept(ctx.io((Invocable&&)i)))
{
  return
    ctx.io((Invocable&&)i) |
    ::stdexec::then([syscall](const ::io_uring_cqe& cqe) {
      if (cqe.res < 0) {
        throw detail::io_uring_context::exception(
          -cqe.res,
          syscall);
      }
      return std::ref(cqe);
    });
}

}
