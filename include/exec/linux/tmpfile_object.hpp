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

#include <memory>
#include <new>
#include <string>
#include <system_error>
#include <utility>
#include "file_descriptor.hpp"
#include "generate_random_path_until.hpp"
#include "io_uring_context.hpp"
#include "open.hpp"
#include "../coroutine_sender.hpp"
#include "../enter_scope_sender.hpp"
#include "../../stdexec/execution.hpp"

#include <fcntl.h>
#include <sys/stat.h>

namespace exec {

struct tmpfile_object {
  struct type : file_descriptor {
    explicit type(io_uring_context& ctx, int fd, std::string path) noexcept
      : file_descriptor(ctx, fd),
        path_(std::move(path))
    {}
    constexpr const std::string& path() const noexcept {
      return path_;
    }
  private:
    std::string path_;
  };

  explicit tmpfile_object(
    io_uring_context& ctx,
    std::string path_template,
    int flags = O_CREAT | O_EXCL | O_RDWR,
    ::mode_t mode = 0600) noexcept
    : ctx_(ctx),
      path_template_(std::move(path_template)),
      flags_(flags),
      mode_(mode),
      dirfd_(AT_FDCWD)
  {}

  explicit tmpfile_object(
    io_uring_context& ctx,
    int dirfd,
    std::string relative_template,
    int flags = O_CREAT | O_EXCL | O_RDWR,
    ::mode_t mode = 0600) noexcept
    : ctx_(ctx),
      path_template_(std::move(relative_template)),
      flags_(flags),
      mode_(mode),
      dirfd_(dirfd)
  {}

  template<typename Self>
  ::exec::enter_scope_sender auto operator()(this Self&& self, type* storage) noexcept {
    auto path_template = std::forward<Self>(self).path_template_;
    auto& ctx = self.ctx_;
    const int flags = self.flags_;
    const ::mode_t mode = self.mode_;
    const int dirfd = self.dirfd_;
    return
      ::exec::generate_random_path_until(
        ctx,
        std::move(path_template),
        [&ctx, storage, flags, mode, dirfd](std::string&& path) -> coroutine_sender<bool> {
          try {
            const int fd = co_await ::exec::open(
              ctx,
              dirfd,
              path.c_str(),
              flags,
              mode);
            new(storage) type(ctx, fd, std::move(path));
          } catch (const std::system_error& ex) {
            if (ex.code() == std::errc::file_exists) {
              co_return false;
            }
            throw;
          }
          co_return true;
        }) |
      ::STDEXEC::then([storage]() noexcept {
        auto* ptr = std::launder(reinterpret_cast<type*>(storage));
        return ptr->close();
      });
  }
private:
  io_uring_context& ctx_;
  std::string path_template_;
  int flags_;
  ::mode_t mode_;
  int dirfd_;
};

} // namespace exec
