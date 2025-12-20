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

#include <coroutine>
#include <cstddef>
#include <exception>
#include <tuple>
#include <type_traits>
#include <utility>
#include <stdexec/execution.hpp>
#include "storage_for_completion_signatures.hpp"

namespace exec {

template<typename>
struct coroutine_sender;

namespace detail::coroutine_sender {

template<typename>
struct filter_completion_signature;
template<typename... Args>
struct filter_completion_signature<::stdexec::set_value_t(Args...)>
  : std::type_identity<::stdexec::set_value_t(Args...)> {};

template<typename T>
struct value_completion_signatures : std::type_identity<
  ::stdexec::completion_signatures<::stdexec::set_value_t(T)>> {};
template<>
struct value_completion_signatures<void> : std::type_identity<
  ::stdexec::completion_signatures<::stdexec::set_value_t()>> {};
template<typename... Args>
struct value_completion_signatures<::stdexec::completion_signatures<Args...>>
  : std::type_identity<
      ::stdexec::completion_signatures<
        typename filter_completion_signature<Args>::type...>> {};

template<typename T>
using value_completion_signatures_t =
  typename value_completion_signatures<T>::type;

struct return_void {};

template<typename Signatures>
using storage_for_completion_signatures_t =
  ::exec::storage_for_completion_signatures<
    ::stdexec::transform_completion_signatures<
      value_completion_signatures_t<Signatures>,
      ::stdexec::completion_signatures<
        ::stdexec::set_error_t(std::exception_ptr)>>>;

template<typename Signatures>
using completion_signatures = ::stdexec::transform_completion_signatures<
  typename storage_for_completion_signatures_t<Signatures>::
    completion_signatures,
  ::stdexec::completion_signatures<
    ::stdexec::set_stopped_t()>>;

template<typename Signatures>
struct arrive_invocable {
  using type_ = storage_for_completion_signatures_t<Signatures>;
  template<typename... Args>
    requires requires(type_ t) {
      t.arrive(::stdexec::set_value, std::declval<Args>()...);
    }
  constexpr void operator()(Args&&... args) const noexcept {
    storage_.arrive(::stdexec::set_value, std::forward<Args>(args)...);
  }
  type_& storage_;
};

template<typename Object>
concept is_return_void = std::is_same_v<
  std::remove_cvref_t<Object>,
  return_void>;

template<typename Object, typename Signatures>
concept is_return_single =
  !is_return_void<Object> &&
  requires(storage_for_completion_signatures_t<Signatures> storage) {
    storage.arrive(::stdexec::set_value, std::declval<Object>());
  };

//  This is because std::apply isn't SFINAE-friendly
template<typename Object, typename Signatures, typename>
struct check_apply;
template<typename Object, typename Signatures, std::size_t... Is>
struct check_apply<Object, Signatures, std::index_sequence<Is...>> :
  std::bool_constant<
    requires(const arrive_invocable<Signatures> i) {
      i(std::get<Is>(std::declval<Object>())...);
    }> {};

template<typename Object, typename Signatures>
concept is_return_multiple =
  !is_return_void<Object> &&
  check_apply<
    Object,
    Signatures,
    std::make_index_sequence<std::tuple_size<Object>::value>>::value;

template<typename>
struct promise;

using env = ::stdexec::prop<::stdexec::get_stop_token_t, ::stdexec::inplace_stop_token>;

template<typename Signatures>
struct operation_state_base {
  storage_for_completion_signatures_t<Signatures> storage;
  virtual void complete() noexcept = 0;
  virtual void stopped(promise<Signatures>&) noexcept = 0;
  virtual env get_env() const noexcept = 0;
};

struct on_stop_request {
  void operator()() && noexcept {
    source_.request_stop();
  }
  ::stdexec::inplace_stop_source& source_;
};

template<typename StopToken>
struct operation_state_stop_source_base {
  template<typename Receiver>
  ::stdexec::inplace_stop_token get_stop_token(const Receiver&) const noexcept {
    return source_.get_token();
  }
  template<typename Receiver>
  void attach(const Receiver& r) noexcept {
    STDEXEC_ASSERT(!callback_);
    callback_.emplace(
      ::stdexec::get_stop_token(::stdexec::get_env(r)),
      on_stop_request{source_});
  }
  void detach() noexcept {
    STDEXEC_ASSERT(callback_);
    callback_.reset();
  }
private:
  ::stdexec::inplace_stop_source source_;
  std::optional<
    ::stdexec::stop_callback_for_t<
      StopToken,
      on_stop_request>> callback_;
};

template<typename StopToken>
  requires ::stdexec::unstoppable_token<StopToken>
struct operation_state_stop_source_base<StopToken> {
  template<typename Receiver>
  static ::stdexec::inplace_stop_token get_stop_token(const Receiver&) noexcept {
    return {};
  }
  static void detach() noexcept {}
  template<typename Receiver>
  static void attach(const Receiver&) noexcept {}
};

template<typename StopToken>
  requires std::is_same_v<::stdexec::inplace_stop_token, StopToken>
struct operation_state_stop_source_base<StopToken> {
  template<typename Receiver>
  ::stdexec::inplace_stop_token get_stop_token(const Receiver& r) const noexcept {
    return ::stdexec::get_stop_token(
      ::stdexec::get_env(r));
  }
  static void detach() noexcept {}
  template<typename Receiver>
  static void attach(const Receiver&) noexcept {}
};

template<typename Signatures>
struct promise {
  constexpr auto unhandled_stopped() noexcept {
    STDEXEC_ASSERT(op);
    op->stopped(*this);
    return std::noop_coroutine();
  }
  //  TODO: Mappings?
  template<typename Sender>
  constexpr auto await_transform(Sender&& sender) noexcept(
    noexcept(
      ::stdexec::as_awaitable(
        std::declval<Sender>(),
        std::declval<promise&>())))
  {
    return ::stdexec::as_awaitable(std::forward<Sender>(sender), *this);
  }
  auto get_env() const noexcept {
    STDEXEC_ASSERT(op);
    return op->get_env();
  }
  constexpr auto get_return_object() noexcept {
    return ::exec::coroutine_sender(*this);
  }
  constexpr std::suspend_always initial_suspend() noexcept {
    STDEXEC_ASSERT(!op);
    return {};
  }
  constexpr std::suspend_never final_suspend() noexcept {
    STDEXEC_ASSERT(op);
    op->complete();
    return {};
  }
  template<is_return_void Object>
  constexpr void return_value(Object&&) noexcept {
    STDEXEC_ASSERT(op);
    op->storage.arrive(::stdexec::set_value);
  }
  template<is_return_single<Signatures> Object>
  constexpr void return_value(Object&& o) noexcept {
    STDEXEC_ASSERT(op);
    op->storage.arrive(::stdexec::set_value, std::forward<Object>(o));
  }
  template<is_return_multiple<Signatures> Object>
  constexpr void return_value(Object&& o) noexcept {
    STDEXEC_ASSERT(op);
    std::apply(
      arrive_invocable<Signatures>{op->storage},
      std::forward<Object>(o));
  }
  void unhandled_exception() noexcept {
    STDEXEC_ASSERT(op);
    op->storage.arrive(::stdexec::set_error, std::current_exception());
  }
  operation_state_base<Signatures>* op{nullptr};
};

template<typename Signatures, ::stdexec::receiver Receiver>
  requires
    ::stdexec::receiver_of<
      Receiver,
      completion_signatures<Signatures>>
class operation_state :
  operation_state_base<Signatures>,
  operation_state_stop_source_base<
    ::stdexec::stop_token_of_t<
      ::stdexec::env_of_t<Receiver>>>
{
  using base_ = operation_state_base<Signatures>;
  using promise_type_ = promise<Signatures>;
  using stop_token_type_ = ::stdexec::stop_token_of_t<
    ::stdexec::env_of_t<Receiver>>;
  using stop_token_base_ = operation_state_stop_source_base<stop_token_type_>;
  promise_type_* promise_;
  Receiver r_;
  virtual void complete() noexcept override {
    stop_token_base_::detach();
    std::move(base_::storage).complete(std::move(r_));
  }
  virtual void stopped(promise_type_& promise) noexcept override {
    STDEXEC_ASSERT(!promise_);
    //  This causes the operation state to clean up the coroutine frame
    promise_ = &promise;
    stop_token_base_::detach();
    ::stdexec::set_stopped(std::move(r_));
  }
  virtual env get_env() const noexcept override {
    return env(
      ::stdexec::get_stop_token,
      stop_token_base_::get_stop_token(r_));
  }
public:
  constexpr explicit operation_state(
    promise<Signatures>& promise,
    Receiver r) noexcept
    : promise_(&promise),
      r_(std::move(r))
  {}
  operation_state(const operation_state&) = delete;
  operation_state& operator=(const operation_state&) = delete;
  constexpr ~operation_state() noexcept {
    if (promise_) {
      std::coroutine_handle<promise_type_>::from_promise(*promise_).destroy();
    }
  }
  void start() & noexcept {
    STDEXEC_ASSERT(promise_);
    auto&& promise = *std::exchange(promise_, nullptr);
    STDEXEC_ASSERT(!promise.op);
    promise.op = this;
    stop_token_base_::attach(r_);
    std::coroutine_handle<promise_type_>::from_promise(promise).resume();
  }
};

}

inline constexpr detail::coroutine_sender::return_void coroutine_sender_void;

template<typename Signatures>
struct coroutine_sender {
  using sender_concept = ::stdexec::sender_t;
  using promise_type = detail::coroutine_sender::promise<Signatures>;
  explicit constexpr coroutine_sender(promise_type& promise) noexcept
    : promise_(&promise)
  {}
  constexpr coroutine_sender(coroutine_sender&& other) noexcept
    : promise_(std::exchange(other.promise_, nullptr))
  {}
  coroutine_sender& operator=(coroutine_sender&) = delete;
  constexpr ~coroutine_sender() noexcept {
    if (promise_) {
      std::coroutine_handle<promise_type>::from_promise(*promise_).destroy();
    }
  }
  template<typename Env>
  consteval detail::coroutine_sender::completion_signatures<Signatures>
    get_completion_signatures(const Env&) && noexcept
  {
    return {};
  }
  template<typename Receiver>
  constexpr auto connect(Receiver r) && noexcept {
    STDEXEC_ASSERT(promise_);
    return detail::coroutine_sender::operation_state<Signatures, Receiver>(
      *std::exchange(promise_, nullptr),
      std::move(r));
  }
private:
  promise_type* promise_;
};


} // namespace exec
