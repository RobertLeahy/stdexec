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
#include <array>
#include <cassert>
#include <cstddef>
#include <functional>
#include <tuple>
#include <utility>

#include "child_operation_state.hpp"
#include "inlinable_operation_state.hpp"
#include "like_t.hpp"
#include "object.hpp"
#include "observe.hpp"
#include "../stdexec/execution.hpp"

namespace exec {

namespace detail::construct {

struct observer {
  template<typename Tag, typename... Args>
  constexpr void operator()(const Tag&, const Args&...) const noexcept {}
  template<typename... Args>
  constexpr void operator()(const ::stdexec::set_value_t&, const Args&...) const
    noexcept
  {
    assert(!succeeded_);
    succeeded_ = true;
  }
  bool& succeeded_;
};

template<::stdexec::sender Sender>
constexpr decltype(auto) wrap(Sender&& sender, bool& succeeded) noexcept(
  noexcept(
    ::exec::observe(
      std::declval<Sender>(),
      std::declval<observer>())))
{
  return ::exec::observe(
    std::forward<Sender>(sender),
    observer{succeeded});
}

template<::stdexec::sender... Senders>
inline constexpr bool when_all_noexcept = noexcept(
  ::stdexec::when_all(
    construct::wrap(
      std::declval<Senders>(),
      std::declval<bool&>())...));

template<::stdexec::sender... Senders>
constexpr decltype(auto) when_all(
  std::array<bool, sizeof...(Senders)>& arr,
  Senders&&... senders) noexcept(
    when_all_noexcept<Senders...>)
{
  return [&]<std::size_t... Ns>(std::index_sequence<Ns...>) noexcept(
    when_all_noexcept<Senders...>)
  {
    return ::stdexec::when_all(
      construct::wrap(
        std::forward<Senders>(senders),
        arr[Ns])...);
  }(std::index_sequence_for<Senders...>{});
}

template<::stdexec::sender... Senders>
using when_all_t = decltype(
  construct::when_all(
    std::declval<std::array<bool, sizeof...(Senders)>&>(),
    std::declval<Senders>()...));

template<typename... Args>
using transform_set_value = ::stdexec::completion_signatures<
  ::stdexec::set_value_t(Args...)>;

template<::stdexec::sender... Senders>
struct transform_set_error {
  template<typename T>
  using fn = ::stdexec::completion_signatures<
    ::stdexec::set_error_t(T),
    ::stdexec::set_value_t(
      std::array<bool, sizeof...(Senders)>,
      ::stdexec::set_error_t,
      T)>;
};

template<typename Env, ::stdexec::sender... Senders>
using completion_signatures = ::stdexec::transform_completion_signatures<
  ::stdexec::completion_signatures_of_t<
    when_all_t<Senders...>,
    Env>,
  ::stdexec::completion_signatures<>,
  transform_set_value,
  transform_set_error<Senders...>::template fn,
  ::stdexec::completion_signatures<
    ::stdexec::set_stopped_t(),
    ::stdexec::set_value_t(
      std::array<bool, sizeof...(Senders)>,
      ::stdexec::set_stopped_t)>>;

struct tag {};

template<std::size_t N>
struct array_base {
  std::array<bool, N> constructed{};
};

template<typename Receiver, ::stdexec::sender... Senders>
struct operation_state
  : inlinable_operation_state<
      operation_state<Receiver, Senders...>,
      Receiver>,
    array_base<sizeof...(Senders)>,
    child_operation_state<
      operation_state<Receiver, Senders...>,
      tag,
      ::stdexec::env_of_t<Receiver>,
      when_all_t<Senders...>>
{
private:
  using receiver_base_ = inlinable_operation_state<
    operation_state,
    Receiver>;
  using array_base_ = array_base<sizeof...(Senders)>;
  using env_type_ = ::stdexec::env_of_t<Receiver>;
  using when_all_type_ = when_all_t<Senders...>;
  using child_base_ = child_operation_state<
    operation_state,
    tag,
    env_type_,
    when_all_type_>;
  template<typename Tag, typename... Args>
  constexpr void fail_(Tag tag, Args&&... args) noexcept {
    const array_base_& arr = *this;
    if (std::none_of(
      arr.constructed.begin(),
      arr.constructed.end(),
      std::identity{}))
    {
      tag(std::move(this->get_receiver()), std::forward<Args>(args)...);
      return;
    }
    //  Don't send a reference to ourselves, therefore make a copy on the stack
    auto copy = arr.constructed;
    //  Partial success, i.e. some constructors ran to completion, others didn't
    ::stdexec::set_value(
      std::move(this->get_receiver()),
      std::move(copy),
      std::move(tag),
      std::forward<Args>(args)...);
  }
public:
  constexpr explicit operation_state(
    Receiver r,
    Senders&&... senders) noexcept(
      when_all_noexcept<Senders...> &&
      std::is_nothrow_constructible_v<
        child_base_,
        when_all_type_>)
    : receiver_base_(std::move(r)),
      child_base_(
        construct::when_all(
          static_cast<array_base_&>(*this).constructed,
          std::forward<Senders>(senders)...))
  {}
  void start() & noexcept {
    child_base_& base = *this;
    base.start();
  }
  constexpr env_type_ get_env(tag) noexcept {
    return ::stdexec::get_env(this->get_receiver());
  }
  constexpr void set_value(tag) noexcept {
    ::stdexec::set_value(std::move(this->get_receiver()));
  }
  template<typename... Args>
  constexpr void set_error(tag, Args&&... args) noexcept {
    fail_(::stdexec::set_error, std::forward<Args>(args)...);
  }
  template<typename... Args>
  constexpr void set_stopped(tag, Args&&... args) noexcept {
    fail_(::stdexec::set_stopped, std::forward<Args>(args)...);
  }
};

template<::stdexec::sender... Senders>
class sender {
  template<typename Self, typename Receiver>
  using operation_state_ = operation_state<
    Receiver,
    ::exec::like_t<Self, Senders>...>;
  template<typename Self, typename Receiver>
  constexpr static bool noexcept_ = std::is_nothrow_constructible_v<
    operation_state_<Self, Receiver>,
    Receiver,
    ::exec::like_t<Self, Senders>...>;
public:
  using sender_concept = ::stdexec::sender_t;
  template<typename Self, typename Env>
    requires (::exec::constructor_in<::exec::like_t<Self, Senders>, Env> && ...)
  consteval completion_signatures<
    Env,
    ::exec::like_t<Self, Senders>...> get_completion_signatures(
      this Self&&,
      const Env&) noexcept
  {
    return {};
  }
  template<typename Self, typename Receiver>
    requires
      ::stdexec::receiver_of<
        Receiver,
        ::stdexec::completion_signatures_of_t<
          Self,
          ::stdexec::env_of_t<Receiver>>>
  constexpr operation_state_<Self, Receiver> connect(
    this Self&& self,
    Receiver r) noexcept(noexcept_<Self, Receiver>)
  {
    return std::apply(
      [&](auto&&... senders) noexcept(noexcept_<Self, Receiver>) {
        return operation_state_<Self, Receiver>(
          std::move(r),
          std::forward<decltype(senders)>(senders)...);
      },
      std::forward<Self>(self).senders_);
  }
  std::tuple<Senders...> senders_;
};

}

template<::stdexec::sender... Senders>
constexpr detail::construct::sender<
  std::remove_cvref_t<Senders>...> construct(Senders&&... senders) noexcept(
    (std::is_nothrow_constructible_v<
      std::remove_cvref_t<Senders>,
      Senders> && ...))
{
  return {{std::forward<Senders>(senders)...}};
}

//  We don't need all the overhead of the nary version if there's just one
//  constructor
template<::stdexec::sender Sender>
constexpr Sender&& construct(Sender&& sender) noexcept {
  return std::forward<Sender>(sender);
}

}  // namespace exec
