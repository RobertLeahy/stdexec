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
#include "can_populate_sqe.hpp"
#include "io_uring_context.hpp"
#include "io_uring_context.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

template<can_populate_sqe FD>
::STDEXEC::sender auto close(FD& fd) noexcept {
  return
    fd.context().io([&fd](::io_uring_sqe& sqe) noexcept {
      std::memset(&sqe, 0, sizeof(sqe));
      sqe.opcode = IORING_OP_CLOSE;
      fd.populate_sqe(sqe);
    }) |
    ::STDEXEC::then([](const ::io_uring_cqe& cqe) noexcept {
      STDEXEC_ASSERT(!cqe.res);
      (void)cqe;
    }) |
    ::STDEXEC::unstoppable;
}

} // namespace exec
