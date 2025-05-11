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
#include <optional>
#include <tuple>
#include <type_traits>
#include <variant>
#include <asioexec/asio_config.hpp>
#include <stdexec/execution.hpp>

namespace asioexec {

  namespace detail::let_io_context {

    template<typename>
    struct decay;
    template<typename T>
      requires (
        !std::is_lvalue_reference_v<T> &&
        std::is_constructible_v<std::decay_t<T>, T>)
    struct decay<T> : std::decay<T> {};
    template<typename T>
    struct decay<T&> {
      using type = T&;
    };

    template<typename... Args>
    using transform_set_value = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(typename decay<Args>::type...)>;
    template<typename T>
    using transform_set_error = ::stdexec::completion_signatures<
      ::stdexec::set_error_t(typename decay<T>::type)>;

    template<typename Signatures>
    using completion_signatures = ::stdexec::transform_completion_signatures<
      Signatures,
      ::stdexec::completion_signatures<>,
      transform_set_value,
      transform_set_error>;

    template<typename>
    struct tuple;
    template<typename Tag, typename... Args>
    struct tuple<Tag(Args...)> {
      using type = std::tuple<
        std::remove_cvref_t<Tag>,
        typename decay<Args>::type...>;
    };

    template<typename>
    class storage;
    template<>
    class storage<::stdexec::completion_signatures<>> {
    public:
      template<::stdexec::receiver Receiver>
      static constexpr void complete(const Receiver&) noexcept {
        STDEXEC_UNREACHABLE();
      }
    };
    template<typename... Signatures>
    class storage<::stdexec::completion_signatures<Signatures...>> {
      using storage_type_ = std::variant<
        std::monostate,
        typename tuple<Signatures>::type...>;
      storage_type_ storage_;
      template<typename Tag, typename... Args>
      using tuple_ = typename tuple<Tag(Args...)>::type;
      template<typename Tag, typename... Args>
      static constexpr bool noexcept_ = std::is_nothrow_constructible_v<tuple_<Tag, Args...>>;
      template<typename Receiver, typename... Args>
      constexpr void complete_(Receiver&& r, std::tuple<Args...>&& t) noexcept {
        std::apply(
          [&](const auto& tag, auto&&... args) noexcept {
            tag(
              static_cast<Receiver&&>(r),
              static_cast<decltype(args)&&>(args)...);
          },
          static_cast<std::tuple<Args...>&&>(t));
      }
      template<typename Receiver>
      constexpr void complete_(const Receiver&, const std::monostate&) noexcept
      {
        STDEXEC_UNREACHABLE();
      }
    public:
      template<typename Tag, typename... Args>
      constexpr void arrive(Tag t, Args&&... args) noexcept(noexcept_<Tag, Args...>) {
        STDEXEC_ASSERT(!*this);
        const auto impl = [&]() noexcept(noexcept_<Tag, Args...>) {
          storage_.template emplace<tuple_<Tag, Args...>>(static_cast<Tag&&>(t), static_cast<Args&&>(args)...);
        };
        if constexpr (noexcept(impl())) {
          impl();
        } else {
          try {
            impl();
          } catch (...) {
            storage_ = std::monostate{};
            throw;
          }
        }
      }
      template<typename Receiver>
      constexpr void complete(Receiver&& r) && noexcept {
        STDEXEC_ASSERT(*this);
        std::visit(
          [&](auto&& alternative) noexcept {
            complete_(static_cast<Receiver&&>(r), static_cast<decltype(alternative)&&>(alternative));
          },
          static_cast<storage_type_&&>(storage_));
      }
      constexpr explicit operator bool() const noexcept {
        return !std::holds_alternative<std::monostate>(storage_);
      }
    };

    //  This should eventually be replaced by an inlinable receiver
    template<typename State>
    class receiver {
      State& self_;
    public:
      using receiver_concept = ::stdexec::receiver_t;
      constexpr explicit receiver(State& self) noexcept : self_(self) {}
      template<typename T>
      //  requires requires(State s) {
      //    { s.set_error(std::declval<T>()) } noexcept;
      //  }
      constexpr void set_error(T&& t) && noexcept {
        self_.set_error(static_cast<T&&>(t));
      }
      template<typename... Args>
      //  requires requires(State s) {
      //    { s.set_value(std::declval<Args>()...) } noexcept;
      //  }
      constexpr void set_value(Args&&... args) && noexcept {
        self_.set_value(static_cast<Args&&>(args)...);
      }
      template<typename... Args>
      constexpr void set_stopped(Args&&... args) && noexcept /*requires requires(State s) {
        { s.set_stopped() } noexcept;
      }*/
      {
        self_.set_stopped(static_cast<Args&&>(args)...);
      }
      template<typename... Args>
      decltype(auto) get_env(Args&&... args) const noexcept /*requires requires(State s) {
        s.get_env();
      }*/
      {
        return self_.get_env(static_cast<Args&&>(args)...);
      }
    };
    
    template<typename Invocable>
    concept invocable =
      std::invocable<Invocable, asio_impl::io_context&> &&
      requires(asio_impl::io_context& ctx) {
        { std::invoke(std::declval<Invocable>(), ctx) } -> ::stdexec::sender;
      };

    template<typename T>
    using invoke_result_t = std::invoke_result_t<
      T,
      asio_impl::io_context&>;

    template<typename Sender, typename Receiver>
    class operation_state {
      using receiver_ = receiver<operation_state>;
      using operation_state_ = ::stdexec::connect_result_t<Sender, receiver_>;
      using completion_signatures_ = completion_signatures<
        ::stdexec::completion_signatures_of_t<
          Sender,
          ::stdexec::env_of_t<Receiver>>>;
      Receiver r_;
      asio_impl::io_context ctx_;
      operation_state_ op_;
      storage<completion_signatures_> storage_;
    public:
      template<typename Invocable>
      explicit operation_state(Invocable&& i, Receiver r)
        : r_(static_cast<Receiver&&>(r)),
          op_(
            ::stdexec::connect(
              std::invoke(static_cast<Invocable>(i)),
              receiver_(*this)))
      {}
      decltype(auto) get_env() const noexcept {
        return ::stdexec::get_env(r_);
      }
      template<typename... Args>
      void set_value(Args&&... args) noexcept {
        storage_.arrive(::stdexec::set_value, static_cast<Args&&>(args)...);
      }
      template<typename... Args>
      void set_error(Args&&... args) noexcept {
        storage_.arrive(::stdexec::set_error, static_cast<Args&&>(args)...);
      }
      template<typename... Args>
      void set_stopped(Args&&... args) noexcept {
        storage_.arrive(::stdexec::set_stopped, static_cast<Args&&>(args)...);
      }
      void start() & noexcept {
        ::stdexec::start(op_);
        [&]() noexcept {
          (void)ctx_.run();
        }();
        storage_.complete(static_cast<Receiver>(r_));
      }
    };

    template<typename Invocable>
    class sender {
      Invocable i_;
    public:
      using sender_concept = ::stdexec::sender_t;
      template<typename T>
        requires std::constructible_from<Invocable, T>
      constexpr explicit sender(T&& t) noexcept(
        std::is_nothrow_constructible_v<Invocable, T>)
        : i_(static_cast<T>(t))
      {}
      template<typename Env>
      constexpr completion_signatures<
        ::stdexec::completion_signatures_of_t<
          invoke_result_t<Invocable>,
          Env>> get_completion_signatures(const Env&) && noexcept
      {
        return {};
      }
      template<typename Env>
      constexpr completion_signatures<
        ::stdexec::completion_signatures_of_t<
          invoke_result_t<const Invocable&>,
          Env>> get_completion_signatures(const Env&) const& noexcept
      {
        return {};
      }
      template<typename Receiver>
        requires ::stdexec::receiver_of<
          Receiver,
          ::stdexec::completion_signatures_of_t<
            sender,
            ::stdexec::env_of_t<Receiver>>>
      auto connect(Receiver r) && {
        return operation_state<
          invoke_result_t<Invocable>,
          Receiver>(
            static_cast<Invocable&&>(i_),
            static_cast<Receiver&&>(r));
      }
      template<typename Receiver>
        requires ::stdexec::receiver_of<
          Receiver,
          ::stdexec::completion_signatures_of_t<
            const sender&,
            ::stdexec::env_of_t<Receiver>>>
      auto connect(Receiver r) const& {
        return operation_state<
          invoke_result_t<const Invocable&>,
          Receiver>(
            i_,
            static_cast<Receiver&&>(r));
      }
    };

  }

  template<typename Invocable>
    requires
      std::constructible_from<
        std::decay_t<Invocable>,
        Invocable>
  constexpr auto let_io_context(Invocable&& i) noexcept(
    std::is_nothrow_constructible_v<
      std::decay_t<Invocable>,
      Invocable>)
  {
    return detail::let_io_context::sender<
      std::decay_t<Invocable>>(
        static_cast<Invocable&&>(i));
  }

}
