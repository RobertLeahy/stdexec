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

#include <type_traits>
#include <utility>

#include "is_nothrow_connectable.hpp"
#include "../stdexec/execution.hpp"

namespace exec {

namespace detail::constructor_in {

template<typename... Args>
using transform_set_value = ::stdexec::completion_signatures<
  ::stdexec::set_value_t(Args...)>;

template<typename Arg>
using transform_set_error = ::stdexec::completion_signatures<>;

}

template<typename Sender, typename Env>
concept constructor_in =
  ::stdexec::sender_in<Sender, Env> &&
  std::is_same_v<
    ::stdexec::completion_signatures<
      ::stdexec::set_value_t()>,
    ::stdexec::transform_completion_signatures<
      ::stdexec::completion_signatures_of_t<
        Sender,
        Env>,
      ::stdexec::completion_signatures<>,
      detail::constructor_in::transform_set_value,
      detail::constructor_in::transform_set_error,
      ::stdexec::completion_signatures<>>>;

template<typename Sender, typename Env>
concept destructor_in =
  ::stdexec::sender_in<Sender, Env> &&
  is_nothrow_connectable_v<Sender, Env> &&
  std::is_same_v<
    ::stdexec::completion_signatures<
      ::stdexec::set_value_t()>,
    ::stdexec::completion_signatures_of_t<
      Sender,
      Env>>;

template<typename Object>
concept object =
  std::is_move_constructible_v<Object> &&
  std::is_destructible_v<Object> &&
  //  Non-void case
  (
    !std::is_same_v<typename Object::type, void> &&
    requires(Object o) {
      { o.construct(std::declval<void*>()) } -> ::stdexec::sender;
      { o.destroy(std::declval<typename Object::type*>()) } noexcept ->
        ::stdexec::sender;
    }) ||
  //  Void case
  requires(Object o) {
    { o.construct() } -> ::stdexec::sender;
    { o.destroy() } noexcept -> ::stdexec::sender;
  };

template<typename Object, typename Env>
concept object_in =
  object<Object> &&
  //  Non-void case
  requires(Object o) {
    { o.construct(std::declval<void*>()) } -> constructor_in<Env>;
    { o.destroy(std::declval<typename Object::type*>()) } noexcept ->
      destructor_in<Env>;
  } ||
  //  Void case
  requires(Object o) {
    { o.construct() } -> constructor_in<Env>;
    { o.destroy() } noexcept -> destructor_in<Env>;
  };

template<typename Object>
concept void_object =
  object<Object> &&
  (
    !requires(Object o) {
      typename Object::type;
    } ||
    std::is_same_v<typename Object::type, void>);

}  // namespace exec
