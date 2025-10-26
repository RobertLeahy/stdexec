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
#include <tuple>
#include <type_traits>
#include <utility>

namespace exec {

template<typename T, typename... Args>
  requires std::is_constructible_v<T, Args...>
struct sync_object {
  using type = T;
  template<typename... Ts>
    requires (std::is_constructible_v<Args, Ts> && ...)
  constexpr explicit sync_object(Ts&&... ts) noexcept(
    (std::is_nothrow_constructible_v<Args, Ts> && ...))
    : args_(std::forward<Ts>(ts)...)
  {}
  constexpr ::stdexec::sender auto construct(void* const ptr) noexcept {
    return ::stdexec::just() | ::stdexec::then([&, ptr]() noexcept(
      std::is_nothrow_constructible_v<T, Args...>)
    {
      new(ptr) T(std::make_from_tuple<T>(std::move(args_)));
    });
  }
  constexpr ::stdexec::sender auto destroy(T* const ptr) noexcept {
    return ::stdexec::just() | ::stdexec::then([ptr]() noexcept {
      ptr->~T();
    });
  }
private:
  std::tuple<Args...> args_;
};

}  // namespace exec
