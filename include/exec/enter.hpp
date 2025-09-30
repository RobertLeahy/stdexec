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

#include <optional>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>

#include "enter_sender.hpp"
#include "exit_sender.hpp"
#include "like_t.hpp"
#include "storage_for_completion_signatures.hpp"
#include "variant_sender.hpp"
#include "../stdexec/execution.hpp"

namespace exec {

namespace detail::enter {

template<typename Env, ::exec::enter_sender_in<Env>... Senders>
using exit_sender_t = decltype(
  ::stdexec::when_all(
    std::declval<
      std::remove_cvref_t<
        ::exec::exit_sender_of_t<Senders, Env>>>()...));

template<typename Sender, typename Env>
  requires ::exec::enter_sender_in<Sender, Env>
using exit_sender_of_t = std::remove_cvref_t<
  ::exec::exit_sender_of_t<Sender, Env>>;

template<::exec::enter_sender Enter, ::exec::exit_sender Exit>
constexpr ::stdexec::sender auto wrap(
  Enter&& s,
  std::optional<Exit>& o) noexcept(
    std::is_nothrow_constructible_v<
      std::remove_cvref_t<Enter>,
      Enter>)
{
  return (Enter&&)s | ::stdexec::then([&o](auto&& s) noexcept {
    o.emplace((decltype(s)&&)s);
  });
}

template<typename Sender, typename Env>
  requires ::exec::enter_sender_in<Sender, Env>
using exit_sender_storage_t = std::optional<
  exit_sender_of_t<Sender, Env>>;

template<typename Env, ::exec::enter_sender_in<Env>... Senders>
using when_all_t = decltype(
  ::stdexec::when_all(
    enter::wrap(
      std::declval<Senders>(),
      std::declval<exit_sender_storage_t<Senders, Env>&>())...));

template<typename... Args>
using transform_set_value = ::stdexec::completion_signatures<>;

template<typename Env, ::exec::enter_sender_in<Env>... Senders>
using storage_for_completion_signatures =
  ::exec::storage_for_completion_signatures<
    ::stdexec::transform_completion_signatures<
      ::stdexec::completion_signatures_of_t<
        when_all_t<Env, Senders...>,
        Env>,
      ::stdexec::completion_signatures<>,
      transform_set_value>>;

template<typename Env, ::exec::enter_sender_in<Env>... Senders>
using completion_signatures =
  ::stdexec::transform_completion_signatures<
    typename storage_for_completion_signatures<Env, Senders...>::completion_signatures,
    ::stdexec::completion_signatures<
      ::stdexec::set_value_t(exit_sender_t<Env, Senders...>)>>;

template<::exec::exit_sender Sender>
constexpr ::stdexec::sender auto get_exit_sender(std::optional<Sender>&& o)
  noexcept
{
  auto noop = ::stdexec::just();
  using return_type = ::exec::variant_sender<
    decltype(noop),
    Sender>;
  if (o) {
    return return_type(*std::move(o));
  }
  return return_type(std::move(noop));
}

template<
  ::stdexec::receiver Receiver,
  ::exec::enter_sender_in<::stdexec::env_of_t<Receiver>>... Senders>
class operation_state {
  using env_type_ = ::stdexec::env_of_t<Receiver>;
  static_assert(::exec::exit_sender_in<
    exit_sender_t<env_type_, Senders...>,
    env_type_>);
  template<typename T>
  using exit_sender_storage_type_ = exit_sender_storage_t<T, env_type_>;
  using when_all_sender_ = when_all_t<env_type_, Senders...>;
  constexpr void clean_up_() noexcept {
    ::stdexec::start(
      op_.template emplace<clean_up_wrapper_>(
        clean_up_receiver_{*this},
        std::move(exit_senders_)).op_);
  }
  struct when_all_receiver_ {
    using receiver_concept = ::stdexec::receiver_t;
    constexpr void set_value() && noexcept {
      std::apply(
        [&](auto&&... storage) noexcept {
          ::stdexec::set_value(
            std::move(self_.r_),
            ::stdexec::when_all(*(decltype(storage)&&)storage...));
        },
        std::move(self_.exit_senders_));
    }
    template<typename... Args>
    constexpr void set_error(Args&&... args) && noexcept {
      self_.completion_.arrive(
        ::stdexec::set_error,
        (Args&&)args...);
      self_.clean_up_();
    }
    template<typename... Args>
    constexpr void set_stopped(Args&&... args) && noexcept {
      self_.completion_.arrive(
        ::stdexec::set_stopped,
        (Args&&)args...);
      self_.clean_up_();
    }
    constexpr env_type_ get_env() const noexcept {
      return ::stdexec::get_env(self_.r_);
    }
    operation_state& self_;
  };
  using when_all_op_ = ::stdexec::connect_result_t<
    when_all_sender_,
    when_all_receiver_>;
  using clean_up_sender_ = decltype(
    ::stdexec::when_all(
      enter::get_exit_sender(
        std::declval<exit_sender_storage_type_<Senders>>())...));
  struct clean_up_receiver_ {
    using receiver_concept = ::stdexec::receiver_t;
    constexpr void set_value() && noexcept {
      std::move(self_.completion_).complete(std::move(self_.r_));
    }
    constexpr env_type_ get_env() const noexcept {
      return ::stdexec::get_env(self_.r_);
    }
    operation_state& self_;
  };
  using clean_up_op_ = ::stdexec::connect_result_t<
    clean_up_sender_,
    clean_up_receiver_>;
  Receiver r_;
  storage_for_completion_signatures<env_type_, Senders...> completion_;
  using exit_senders_type_ = std::tuple<exit_sender_storage_type_<Senders>...>;
  exit_senders_type_ exit_senders_;
  struct when_all_wrapper_ {
    static constexpr bool factory_nothrow_ = noexcept(
      ::stdexec::when_all(
        enter::wrap(
          std::declval<Senders>(),
          std::declval<exit_sender_storage_type_<Senders>&>())...));
    static constexpr bool nothrow_ =
      factory_nothrow_ &&
      noexcept(
        ::stdexec::connect(
          std::declval<when_all_sender_>(),
          std::declval<when_all_receiver_>()));
    when_all_op_ op_;
    constexpr explicit when_all_wrapper_(
      when_all_receiver_ r,
      exit_senders_type_& storage,
      Senders&&... senders) noexcept(nothrow_)
      : op_(
          ::stdexec::connect(
            std::apply(
              [&](auto&&... storage) noexcept(factory_nothrow_) {
                return ::stdexec::when_all(
                  enter::wrap((Senders&&)senders, storage)...);
              },
              storage),
            std::move(r)))
    {}
  };
  struct clean_up_wrapper_ {
    clean_up_op_ op_;
    constexpr explicit clean_up_wrapper_(
      clean_up_receiver_ r,
      exit_senders_type_&& storage) noexcept
      : op_(
        ::stdexec::connect(
          std::apply(
            [&](auto&&... storage) noexcept {
              return ::stdexec::when_all(
                enter::get_exit_sender(
                  (decltype(storage)&&)storage)...);
            },
            std::move(storage)),
          std::move(r)))
    {}
  };
  std::variant<when_all_wrapper_, clean_up_wrapper_> op_;
public:
  constexpr explicit operation_state(Receiver r, Senders&&... senders) noexcept(
    std::is_nothrow_constructible_v<
      when_all_wrapper_,
      when_all_receiver_,
      exit_senders_type_&,
      Senders...>)
    : r_(std::move(r)),
      op_(
        std::in_place_type<when_all_wrapper_>,
        when_all_receiver_{*this},
        exit_senders_,
        (Senders&&)senders...)
  {}
  void start() & noexcept {
    const auto ptr = std::get_if<when_all_wrapper_>(&op_);
    ::stdexec::start(ptr->op_);
  }
};

template<::exec::enter_sender... Senders>
struct sender {
  using sender_concept = ::stdexec::sender_t;
  std::tuple<Senders...> senders;
  template<typename Self, typename Env>
    requires
      (::exec::enter_sender_in<
        ::exec::like_t<Self, Senders>,
        Env> && ...)
  consteval
    detail::enter::completion_signatures<Env, ::exec::like_t<Self, Senders>...>
    get_completion_signatures(
      this Self&&,
      const Env&) noexcept
  {
    return {};
  }
private:
  template<typename Self, typename Receiver>
  using operation_state_ = operation_state<
    Receiver,
    ::exec::like_t<Self, Senders>...>;
  template<typename Self, typename Receiver>
  static constexpr bool nothrow_ = std::is_nothrow_constructible_v<
    operation_state_<Self, Receiver>,
    Receiver,
    ::exec::like_t<Self, Senders>...>;
public:
  template<typename Self, typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        Self,
        ::stdexec::env_of_t<Receiver>>>
  constexpr auto connect(this Self&& self, Receiver r) noexcept(
    nothrow_<Self, Receiver>)
  {
    return std::apply(
      [&](auto&&... senders) noexcept(nothrow_<Self, Receiver>) {
        return operation_state<Receiver, decltype(senders)...>(
          std::move(r),
          (decltype(senders)&&)senders...);
      },
      ((Self&&)self).senders);
  }
};

}

struct enter_t {
  enter_sender auto operator()() const noexcept {
    return ::stdexec::just(::stdexec::just());
  }
  template<enter_sender Sender>
  constexpr auto operator()(Sender&& s) const noexcept(
    std::is_nothrow_constructible_v<
      std::remove_cvref_t<Sender>,
      Sender>)
  {
    return (Sender&&)s;
  }
  template<enter_sender... Senders>
  constexpr enter_sender auto operator()(Senders&&... s) const noexcept(
    (std::is_nothrow_constructible_v<
      std::remove_cvref_t<Senders>,
      Senders> && ...))
  {
    return detail::enter::sender<std::remove_cvref_t<Senders>...>{
      {(Senders&&)s...}};
  }
};
inline constexpr enter_t enter;

}  // namespace exec
