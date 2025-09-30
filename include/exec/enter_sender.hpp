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

#include <concepts>
#include <type_traits>
#include <utility>

#include "exit_sender.hpp"
#include "../stdexec/execution.hpp"

namespace exec {

template<typename Sender>
concept enter_sender = ::stdexec::sender<Sender>;

namespace detail::exit_sender_of {

template<typename Env, typename... Args>
struct transform_set_value_impl;
template<typename Env, ::exec::exit_sender_in<Env> Sender>
struct transform_set_value_impl<Env, Sender> {
  using type = ::stdexec::completion_signatures<
    ::stdexec::set_value_t(Sender)>;
};

template<typename Env>
struct transform_set_value {
  template<typename... Args>
  using fn = transform_set_value_impl<Env, Args...>::type;
};

template<typename T>
using transform_set_error = ::stdexec::completion_signatures<>;

template<typename Signatures>
struct impl;
template<typename Sender>
struct impl<
  ::stdexec::completion_signatures<
    ::stdexec::set_value_t(Sender)>>
{
  using type = Sender;
};

}

template<enter_sender Constructor, typename Env>
using exit_sender_of_t = detail::exit_sender_of::impl<
  ::stdexec::transform_completion_signatures<
    ::stdexec::completion_signatures_of_t<Constructor, Env>,
    ::stdexec::completion_signatures<>,
    detail::exit_sender_of::transform_set_value<Env>::template fn,
    detail::exit_sender_of::transform_set_error,
    ::stdexec::completion_signatures<>>>::type;

template<typename Sender, typename Env>
concept enter_sender_in =
  enter_sender<Sender> &&
  ::stdexec::sender_in<Sender, Env> &&
  requires {
    typename exit_sender_of_t<Sender, Env>;
  };

}  // namespace exec
