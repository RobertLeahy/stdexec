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

#include "../stdexec/execution.hpp"

namespace exec {

namespace detail::is_nothrow_connectable {

template<typename Env>
struct receiver {
  using receiver_concept = ::stdexec::receiver_t;
  template<typename... Args>
  void set_value(Args&&...) && noexcept;
  template<typename... Args>
  void set_error(Args&&...) && noexcept;
  template<typename... Args>
  void set_stopped(Args&&...) && noexcept;
  Env get_env() const noexcept;
};

}

template<typename Sender, typename Env>
constexpr inline bool is_nothrow_connectable_v = noexcept(
  ::stdexec::connect(
    std::declval<Sender>(),
    std::declval<detail::is_nothrow_connectable::receiver<Env>>()));

template<typename Sender, typename Env>
struct is_nothrow_connectable : std::bool_constant<
  is_nothrow_connectable_v<Sender, Env>>
{};

} // namespace exec
