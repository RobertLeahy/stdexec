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
#include "is_nothrow_connectable.hpp"
#include "like_t.hpp"
#include "storage_for_completion_signatures.hpp"

#include <concepts>
#include <functional>
#include <tuple>
#include <utility>

namespace exec {

namespace detail::on_execution_context {

template<typename Context>
concept execution_context = requires(Context ctx) {
  { ctx.run() } noexcept -> std::same_as<void>;
  { ctx.finish() } noexcept -> std::same_as<void>;
  { ctx.get_scheduler() } noexcept -> ::stdexec::scheduler;
};
static_assert(execution_context<::stdexec::run_loop>);

template<execution_context Context>
using scheduler_t = decltype(std::declval<Context&>().get_scheduler());

template<execution_context Context, typename Env>
using env_t = ::stdexec::env<
  ::stdexec::prop<
    ::stdexec::get_scheduler_t,
    scheduler_t<Context>>,
  Env>;

template<execution_context Context, ::stdexec::sender Sender, typename Env>
using storage_for_completion_signatures_t =
  ::exec::storage_for_completion_signatures<
    ::stdexec::completion_signatures_of_t<Sender, env_t<Context, Env>>>;

template<execution_context Context, ::stdexec::sender Sender, typename Env>
using completion_signatures_t = typename storage_for_completion_signatures_t<
  Context,
  Sender,
  env_t<Context, Env>>::completion_signatures;

template<
  execution_context Context,
  ::stdexec::sender Sender,
  ::stdexec::receiver Receiver>
    requires
      ::stdexec::sender_in<Sender, env_t<Context, ::stdexec::env_of_t<Receiver>>> &&
      ::stdexec::receiver_of<
        Receiver,
        completion_signatures_t<Context, Sender, ::stdexec::env_of_t<Receiver>>>
class operation_state {
  using receiver_env_type_ = ::stdexec::env_of_t<Receiver>;
  using env_type_ = env_t<Context, receiver_env_type_>;
  using storage_for_completion_signatures_type_ =
    storage_for_completion_signatures_t<Context, Sender, receiver_env_type_>;
  struct receiver_type_ {
    using receiver_concept = ::stdexec::receiver_t;
    operation_state& self_;
    //  Adding the constexpr causes Clang to reject complaining about member
    //  access into an incomplete class (operation_state)
    /*constexpr*/ env_type_ get_env() const noexcept {
      return {
        {::stdexec::get_scheduler, self_.ctx_.get_scheduler()},
        ::stdexec::get_env(self_.r_)};
    }
    template<typename... Args>
    constexpr void set_value(Args&&... args) && noexcept {
      complete_(::stdexec::set_value, std::forward<Args>(args)...);
    }
    template<typename... Args>
    constexpr void set_error(Args&&... args) && noexcept {
      complete_(::stdexec::set_error, std::forward<Args>(args)...);
    }
    template<typename... Args>
    constexpr void set_stopped(Args&&... args) && noexcept {
      complete_(::stdexec::set_stopped, std::forward<Args>(args)...);
    }
  private:
    template<typename... Args>
    constexpr void complete_(Args&&... args) noexcept {
      self_.completion_.arrive(std::forward<Args>(args)...);
      self_.ctx_.finish();
    }
  };
  using operation_state_type_ = ::stdexec::connect_result_t<
    Sender,
    receiver_type_>;
  storage_for_completion_signatures_type_ completion_;
  Context ctx_;
  //  TODO: Inlinable
  [[no_unique_address]]
  Receiver r_;
  operation_state_type_ op_;
public:
  template<typename... Args>
    requires std::constructible_from<Context, Args...>
  constexpr explicit operation_state(
    Sender&& sender,
    Receiver r,
    Args&&... args) noexcept(
      std::is_nothrow_constructible_v<Context, Args...> &&
      ::exec::is_nothrow_connectable_v<Sender, env_type_>)
      : ctx_(std::forward<Args>(args)...),
        r_(std::move(r)),
        op_(
          ::stdexec::connect(
            std::forward<Sender>(sender),
            receiver_type_{*this}))
  {}
  void start() & noexcept {
    ::stdexec::start(op_);
    ctx_.run();
    std::move(completion_).complete(std::move(r_));
  }
};

template<typename Context, ::stdexec::sender Sender, typename... Args>
struct sender {
  using sender_concept = ::stdexec::sender_t;
  Sender s_;
  std::tuple<Args...> args_;
  template<typename Self, typename Env>
  consteval completion_signatures_t<
    Context,
    ::exec::like_t<Self, Sender>,
    Env> get_completion_signatures(this Self&&, const Env&) noexcept
  {
    return {};
  }
  template<typename Self, typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        Self,
        ::stdexec::env_of_t<Receiver>>>
  constexpr auto connect(this Self&& self, Receiver r) noexcept(
    std::is_nothrow_constructible_v<
      operation_state<Context, ::exec::like_t<Self, Sender>, Receiver>,
      ::exec::like_t<Self, Sender>,
      Receiver,
      ::exec::like_t<Self, Args>...>)
  {
    return std::apply([&](auto&&... args) {
      return operation_state<Context, ::exec::like_t<Self, Sender>, Receiver>(
        std::forward<Self>(self).s_,
        std::move(r),
        std::forward<decltype(args)>(args)...);
    }, std::forward<Self>(self).args_);
  }
};

template<execution_context Context, typename... Args>
struct adaptor : ::stdexec::sender_adaptor_closure<adaptor<Context, Args...>> {
  using args_type_ = std::tuple<Args...>;
  args_type_ args_;
  template<typename Self, ::stdexec::sender Sender>
    requires std::constructible_from<args_type_, ::exec::like_t<Self, args_type_>>
  constexpr ::stdexec::sender auto operator()(this Self&& self, Sender&& s)
    noexcept(
      std::is_nothrow_constructible_v<
        std::remove_cvref_t<Sender>,
        Sender> &&
      (std::is_nothrow_constructible_v<args_type_, ::exec::like_t<Self, Args>> && ...))
  {
    return sender<Context, std::remove_cvref_t<Sender>, Args...>{
      std::forward<Sender>(s),
      std::forward<Self>(self).args_};
  }
};

template<execution_context Context>
struct cpo {
  template<typename... Args>
    requires
      (std::is_constructible_v<
        std::decay_t<Args>,
        Args> && ...)
  constexpr adaptor<Context, std::decay_t<Args>...> operator()(Args&&... args)
    const noexcept(
      (std::is_nothrow_constructible_v<
        std::decay_t<Args>,
        Args> && ...))
  {
    return {{std::forward<Args>(args)...}};
  }
};

}

template<detail::on_execution_context::execution_context Context>
inline constexpr detail::on_execution_context::cpo<Context> on_execution_context;

} // namespace exec
