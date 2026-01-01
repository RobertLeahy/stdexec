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

#include <cstddef>
#include <new>
#include <string>
#include <system_error>
#include <utility>
#include "generate_random_path_until.hpp"
#include "io_uring_context.hpp"
#include "io_uring_file_descriptor.hpp"
#include "io_uring_open.hpp"
#include "../coroutine_sender.hpp"
#include "../../stdexec/execution.hpp"

#include <fcntl.h>
#include <sys/stat.h>

namespace exec {

struct io_uring_tmpfile : io_uring_file_descriptor {
  struct type : io_uring_file_descriptor::type {
    explicit type(io_uring_context& ctx, int fd, std::string path) noexcept
      : io_uring_file_descriptor::type(ctx, fd),
        path_(std::move(path))
    {}
    constexpr const std::string& path() const noexcept {
      return path_;
    }
  private:
    std::string path_;
  };
  explicit io_uring_tmpfile(
    io_uring_context& ctx,
    std::string path_template,
    int flags = O_CREAT | O_EXCL | O_RDWR,
    ::mode_t mode = 0600) noexcept
    : io_uring_file_descriptor(ctx),
      path_template_(std::move(path_template)),
      flags_(flags),
      mode_(mode),
      dirfd_(AT_FDCWD)
  {}
  explicit io_uring_tmpfile(
    io_uring_context& ctx,
    int dirfd,
    std::string relative_template,
    int flags = O_CREAT | O_EXCL | O_RDWR,
    ::mode_t mode = 0600) noexcept
    : io_uring_file_descriptor(ctx),
      path_template_(std::move(relative_template)),
      flags_(flags),
      mode_(mode),
      dirfd_(dirfd)
  {}
  ::stdexec::sender auto construct(void* const ptr) noexcept {
    return ::exec::generate_random_path_until(
      ctx_,
      std::move(path_template_),
      [this, ptr](std::string&& path) -> coroutine_sender<bool> {
        try {
          static_assert(sizeof(type) >= sizeof(io_uring_open::type));
          static_assert(alignof(type) >= alignof(io_uring_open::type));
          co_await io_uring_open(
            ctx_,
            dirfd_,
            path.c_str(),
            flags_,
            mode_).construct(ptr);
        } catch (const std::system_error& ex) {
          if (ex.code() == std::errc::file_exists) {
            co_return false;
          }
          throw;
        }
        const auto fd = std::launder(
          static_cast<io_uring_open::type*>(ptr))->native_handle();
        new(ptr) type(ctx_, fd, std::move(path));
        co_return true;
      });
  }
private:
  std::string path_template_;
  int flags_;
  ::mode_t mode_;
  int dirfd_;
};

} // namespace exec
