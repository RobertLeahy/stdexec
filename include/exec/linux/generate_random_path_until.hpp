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

#include <algorithm>
#include <cstddef>
#include <limits>
#include <span>
#include <stdexcept>
#include <string>
#include "file_descriptor.hpp"
#include "io_uring_context.hpp"
#include "random_object.hpp"
#include "read_some.hpp"
#include "../coroutine_sender.hpp"
#include "../lifetime.hpp"
#include "../repeat_effect_until.hpp"
#include "../../stdexec/execution.hpp"

namespace exec::detail::generate_random_path_until {

inline std::span<char> suffix_or_throw(const std::span<char> path_template) {
  const auto it = std::find_if_not(
    path_template.rbegin(),
    path_template.rend(),
    [](const char c) noexcept { return c == 'X'; });
  const auto base = it.base();
  if (base == path_template.end()) {
    throw std::invalid_argument("tmpfile template has no X suffix");
  }
  return std::span<char>(base, path_template.end());
}

inline ::stdexec::sender auto fill_alnum_some(
  file_descriptor& fd,
  const std::span<char> out) noexcept
{
  constexpr const char alphabet[] =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    "abcdefghijklmnopqrstuvwxyz"
    "0123456789";
  constexpr std::size_t alphabet_size = sizeof(alphabet) - 1U;
  constexpr unsigned int byte_range =
    static_cast<unsigned int>(std::numeric_limits<unsigned char>::max()) + 1;
  constexpr unsigned int max_accepted =
    alphabet_size * (byte_range / alphabet_size);
  const auto bytes = std::span<std::byte>(
    reinterpret_cast<std::byte*>(out.data()),
    out.size());
  return
    ::exec::read_some(fd, bytes) |
    ::stdexec::then([out](const std::size_t bytes_read) noexcept {
      const auto view = out.first(bytes_read);
      std::size_t filled = 0;
      for (const unsigned char byte : view) {
        const auto value = static_cast<unsigned char>(byte);
        if (value < max_accepted) {
          view[filled] = alphabet[value % alphabet_size];
          ++filled;
        }
      }
      return filled;
    });
}

inline ::stdexec::sender auto fill_alnum(
  file_descriptor& fd,
  const std::span<char> out) noexcept
{
  return
    ::stdexec::just(out) |
    ::stdexec::let_value([&fd](std::span<char>& out) noexcept {
      return
        ::exec::repeat_effect_until(
          ::stdexec::just() |
          ::stdexec::let_value([&fd, &out]() noexcept {
            return generate_random_path_until::fill_alnum_some(fd, out);
          }) |
          ::stdexec::then([&out](const std::size_t filled) noexcept {
            out = out.subspan(filled);
            return out.empty();
          }));
    });
}

} // namespace exec::detail::generate_random_path_until

namespace exec {

template<typename Invocable>
::stdexec::sender auto generate_random_path_until(
  io_uring_context& ctx,
  std::string path_template,
  Invocable invocable) noexcept
{
  return ::exec::lifetime(
    [path = std::move(path_template), invocable = std::move(invocable)](
      random_object::type& random) mutable -> coroutine_sender<void>
    {
      const auto suffix =
        detail::generate_random_path_until::suffix_or_throw(
          std::span<char>(path.data(), path.size()));
      for (;;) {
        co_await detail::generate_random_path_until::fill_alnum(random, suffix);
        if (co_await invocable(std::move(path))) {
          co_return coroutine_sender_void;
        }
      }
    },
    random_object(ctx));
}

} // namespace exec
