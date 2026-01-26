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
#include "accept.hpp"
#include "file_descriptor.hpp"
#include "io_or_throw.hpp"
#include "io_uring_context.hpp"
#include "../../stdexec/execution.hpp"

#include <sys/socket.h>

namespace exec {

struct accept_object {
  struct type : file_descriptor {
    constexpr explicit type(
      io_uring_context& ctx,
      const int fd,
      const ::sockaddr_storage addr) noexcept
      : file_descriptor(ctx, fd),
        addr_(addr)
    {}
    type(const type&) = delete;
    type& operator=(const type&) = delete;
    constexpr const ::sockaddr_storage& remote_address() const noexcept {
      return addr_;
    }
  private:
    ::sockaddr_storage addr_;
  };

  constexpr explicit accept_object(file_descriptor& fd, const int flags = 0) noexcept
    : fd_(fd),
      flags_(flags)
  {}

  ::STDEXEC::sender auto operator()(type* storage) const noexcept {
    return
      ::exec::accept(fd_, flags_) |
      ::STDEXEC::then([&ctx = fd_.context(), storage](const int fd, const ::sockaddr_storage& addr) noexcept {
        auto* ptr = new(storage) type(ctx, fd, addr);
        return ptr->close();
      });
  }
private:
  file_descriptor& fd_;
  int flags_;
};

} // namespace exec
