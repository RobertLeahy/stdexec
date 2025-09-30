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

#include <functional>
#include <type_traits>
#include <utility>

#include "child_operation_state.hpp"
#include "inlinable_operation_state.hpp"
#include "../stdexec/execution.hpp"

namespace exec {

namespace detail::observe {

template<typename, typename>
struct is_nothrow_observer_impl;
template<typename Function, typename Tag, typename... Args>
struct is_nothrow_observer_impl<Function, Tag(Args...)> {
  constexpr static bool value = std::is_nothrow_invocable_v<
    Function,
    const Tag&,
    const Args&...>;
};
template<typename, typename>
struct is_nothrow_observer;
template<typename Function, typename... Signatures>
struct is_nothrow_observer<
  Function,
  ::stdexec::completion_signatures<Signatures...>>
{
  constexpr static bool value = (
    is_nothrow_observer_impl<Function, Signatures>::value && ...);
};
template<typename Function, typename Signatures>
inline constexpr bool is_nothrow_observer_v =
  is_nothrow_observer<Function, Signatures>::value;

template<typename Function, typename Signatures>
using completion_signatures = ::stdexec::transform_completion_signatures<
  Signatures,
  std::conditional_t<
    is_nothrow_observer_v<Function, Signatures>,
    ::stdexec::completion_signatures<>,
    ::stdexec::completion_signatures<
      ::stdexec::set_error_t(std::exception_ptr)>>>;

template<typename, typename>
struct is_observer_impl;
template<typename Function, typename Tag, typename... Args>
struct is_observer_impl<Function, Tag(Args...)> {
  constexpr static bool value = std::is_invocable_v<
    Function,
    const Tag&,
    const Args&...>;
};
template<typename, typename>
struct is_observer;
template<typename Function, typename... Signatures>
struct is_observer<
  Function,
  ::stdexec::completion_signatures<Signatures...>>
{
  constexpr static bool value = (
    is_observer_impl<Function, Signatures>::value && ...);
};
template<typename Function, typename Signatures>
inline constexpr bool is_observer_v = is_observer<Function, Signatures>::value;

struct tag {};

template<typename Function, typename Sender, typename Receiver>
struct operation_state
  : inlinable_operation_state<
      operation_state<Function, Sender, Receiver>,
      Receiver>,
    child_operation_state<
      operation_state<Function, Sender, Receiver>,
      tag,
      ::stdexec::env_of_t<Receiver>,
      Sender>
{
private:
  using receiver_base_ = inlinable_operation_state<
    operation_state,
    Receiver>;
  using env_type_ = ::stdexec::env_of_t<Receiver>;
  using child_base_ = child_operation_state<
    operation_state,
    tag,
    env_type_,
    Sender>;
  Function f_;
  template<typename Tag, typename... Args>
  constexpr void complete_(const Tag& tag, Args&&... args) noexcept {
    constexpr auto noexcept_ = std::is_nothrow_invocable_v<
      Function,
      const Tag&,
      const Args&...>;
    const auto impl = [&]() noexcept(noexcept_) {
      std::invoke(
        std::move(f_),
        tag,
        std::as_const(args)...);
    };
    if constexpr (noexcept_) {
      impl();
    } else {
      try {
        impl();
      } catch (...) {
        ::stdexec::set_error(
          std::move(this->get_receiver()),
          std::current_exception());
        return;
      }
    }
    tag(std::move(this->get_receiver()), std::forward<Args>(args)...);
  }
public:
  template<typename F>
    requires std::is_constructible_v<Function, F>
  constexpr explicit operation_state(
    F&& f,
    Sender&& s,
    Receiver r) noexcept(
      std::is_nothrow_constructible_v<Function, F> &&
      std::is_nothrow_constructible_v<child_base_, Sender>)
    : receiver_base_(std::move(r)),
      child_base_(std::forward<Sender>(s)),
      f_(std::forward<F>(f))
  {}
  constexpr void start() & noexcept {
    child_base_& base = *this;
    base.start();
  }
  template<typename... Args>
  constexpr void set_value(tag, Args&&... args) noexcept {
    complete_(::stdexec::set_value, std::forward<Args>(args)...);
  }
  template<typename... Args>
  constexpr void set_error(tag, Args&&... args) noexcept {
    complete_(::stdexec::set_error, std::forward<Args>(args)...);
  }
  template<typename... Args>
  constexpr void set_stopped(tag, Args&&... args) noexcept {
    complete_(::stdexec::set_stopped, std::forward<Args>(args)...);
  }
  constexpr env_type_ get_env(tag) noexcept {
    return ::stdexec::get_env(this->get_receiver());
  }
};

template<typename Function, typename Sender>
class sender {
  template<typename Self>
  using child_sender_ = decltype(
    std::forward_like<Self>(std::declval<Sender&>()));
  template<typename Self, typename Receiver>
  using operation_state_ = operation_state<
    Function,
    child_sender_<Self>,
    Receiver>;
public:
  using sender_concept = ::stdexec::sender_t;
  template<typename Self, typename Env>
    requires
      std::is_constructible_v<
        Function,
        decltype(std::forward_like<Self>(std::declval<Function&>()))> &&
      is_observer_v<
        Function,
        ::stdexec::completion_signatures_of_t<
          decltype(std::forward_like<Self>(std::declval<Sender&>())),
          Env>>
  consteval completion_signatures<
    Function,
    ::stdexec::completion_signatures_of_t<
      decltype(std::forward_like<Self>(std::declval<Sender&>())),
      Env>> get_completion_signatures(this Self&&, const Env&) noexcept
  {
    return {};
  }
  template<typename Self, typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        Self,
        ::stdexec::env_of_t<Receiver>>>
  constexpr operation_state_<Self, Receiver> connect(this Self&& self, Receiver r)
    noexcept(
      std::is_nothrow_constructible_v<
        operation_state_<Self, Receiver>,
        decltype(std::forward_like<Self>(std::declval<Function&>())),
        decltype(std::forward_like<Self>(std::declval<Sender&>())),
        Receiver>)
  {
    return operation_state_<Self, Receiver>(
      std::forward<Self>(self).f_,
      std::forward<Self>(self).s_,
      std::move(r));
  }
  Function f_;
  Sender s_;
};

template<typename Function>
struct adaptor : ::stdexec::sender_adaptor_closure<adaptor<Function>> {
  Function f_;
  template<typename Self, ::stdexec::sender Sender>
    requires
      std::is_constructible_v<
        Function,
        decltype(std::forward_like<Self>(std::declval<Function&>()))>
  constexpr sender<Function, std::remove_cvref_t<Sender>> operator()(
    this Self&& self,
    Sender&& s) noexcept(
      std::is_nothrow_constructible_v<
        Function,
        decltype(std::forward_like<Self>(std::declval<Function&>()))> &&
      std::is_nothrow_constructible_v<
        std::remove_cvref_t<Sender>,
        Sender>)
  {
    return {std::forward<Self>(self).f_, std::forward<Sender>(s)};
  }
};

}

inline constexpr struct {
  template<typename Function>
    requires std::is_constructible_v<
      std::remove_cvref_t<Function>,
      Function>
  constexpr detail::observe::adaptor<
    std::remove_cvref_t<Function>> operator()(Function&& f) const noexcept(
      std::is_nothrow_constructible_v<
        std::remove_cvref_t<Function>,
        Function>)
  {
    return {{}, std::forward<Function>(f)};
  }
  template<::stdexec::sender Sender, typename Function>
    requires
      std::is_constructible_v<
        std::remove_cvref_t<Sender>,
        Sender> &&
      std::is_constructible_v<
        std::remove_cvref_t<Function>,
        Function>
  constexpr detail::observe::sender<
    std::remove_cvref_t<Function>,
    std::remove_cvref_t<Sender>> operator()(
      Sender&& sender,
      Function&& f) const noexcept(
        std::is_nothrow_constructible_v<
          std::remove_cvref_t<Sender>,
          Sender> &&
        std::is_nothrow_constructible_v<
          std::remove_cvref_t<Function>,
          Function>)
  {
    return {std::forward<Function>(f), std::forward<Sender>(sender)};
  }
} observe;

}  // namespace exec
