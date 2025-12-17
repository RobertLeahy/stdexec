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

#include <condition_variable>
#include <exception>
#include <mutex>
#include <span>
#include <utility>
#include <variant>
#include <stdexec/execution.hpp>

namespace exec::detail::async_main {

  static std::condition_variable cv;
  static std::mutex m;
  static std::variant<std::monostate, int, std::exception_ptr> result;

  struct receiver {
    using receiver_concept = ::stdexec::receiver_t;
    static void set_value() noexcept {
      complete_(0);
    }
    static void set_error(const int code) noexcept {
      complete_(code);
    }
    static void set_error(std::exception_ptr ex) noexcept {
      complete_(std::move(ex));
    }
    template <typename T>
    static constexpr receiver make_receiver_for(T*) noexcept {
      return {};
    }
   private:
    template <typename T>
    static void complete_(T t) noexcept {
      const std::lock_guard g(detail::async_main::m);
      detail::async_main::result.emplace<T>(std::move(t));
      detail::async_main::cv.notify_one();
    }
  };

} // namespace exec::detail::async_main

int main(const int argc, const char* const * const argv) {
  auto op = ::stdexec::connect(
    ::async_main(std::span(argv, unsigned(argc))), exec::detail::async_main::receiver{});
  ::stdexec::start(op);
  {
    std::unique_lock l(exec::detail::async_main::m);
    exec::detail::async_main::cv.wait(l, [&]() noexcept {
      return !std::holds_alternative<std::monostate>(exec::detail::async_main::result);
    });
  }
  struct visitor {
    [[noreturn]]
    int operator()(std::monostate) const noexcept {
      STDEXEC_UNREACHABLE();
    }
    constexpr int operator()(const int code) const noexcept {
      return code;
    }
    [[noreturn]]
    int operator()(std::exception_ptr ex) const {
      std::rethrow_exception(std::move(ex));
    }
  };
  return std::visit(visitor{}, std::move(exec::detail::async_main::result));
}
