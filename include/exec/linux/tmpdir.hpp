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

#include <string>
#include <system_error>
#include <utility>
#include "generate_random_path_until.hpp"
#include "io_uring_context.hpp"
#include "mkdir.hpp"
#include "../coroutine_sender.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

inline coroutine_sender<std::string> tmpdir(
  io_uring_context& ctx,
  std::string path_template,
  const ::mode_t mode = 0700) noexcept
{
  std::string created;
  co_await ::exec::generate_random_path_until(
    ctx,
    std::move(path_template),
    [&, mode](std::string&& path) -> coroutine_sender<bool> {
      try {
        co_await ::exec::mkdir(ctx, path.c_str(), mode);
      } catch (const std::system_error& ex) {
        if (ex.code() == std::errc::file_exists) {
          co_return false;
        }
        throw;
      }
      created = std::move(path);
      co_return true;
    });
  co_return std::move(created);
}

} // namespace exec
