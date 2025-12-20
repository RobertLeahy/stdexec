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
#include <exception>
#include <functional>
#include <new>
#include <optional>
#include <type_traits>
#include <utility>
#include <variant>
#include <stdexec/execution.hpp>

namespace exec {

namespace detail::coroutine_sender {

template<typename Stored, typename Sent>
struct variant_traits {
  using storage_type = std::variant<
    std::monostate,
    Stored,
    std::exception_ptr>;
  using value_completion_signature = ::stdexec::set_value_t(Sent);
  template<typename T>
  static bool maybe_fail(storage_type&& storage, T& t) noexcept {
    if (const auto ptr = std::get_if<std::exception_ptr>(&storage); ptr) {
      t.set_error(std::move(*ptr));
      return true;
    }
    return false;
  }
  template<typename T>
  static void store(storage_type& storage, T&& t) noexcept {
    try {
      storage.template emplace<Stored>(std::forward<T>(t));
    } catch (...) {
      storage.template emplace<std::exception_ptr>(std::current_exception());
    }
  }
};

template<typename... Args>
struct traits : variant_traits<Args..., Args...> {
  using base = variant_traits<Args..., Args...>;
  template<typename T>
  static void complete(typename base::storage_type&& storage, T& t) noexcept {
    if (!base::maybe_fail(std::move(storage), t)) {
      const auto ptr = std::get_if<Args...>(&storage);
      t.set_value(std::move(*ptr));
    }
  }
};

template<typename... Args>
  requires (std::is_reference_v<Args> && ...)
struct traits<Args...> : variant_traits<
  std::reference_wrapper<std::remove_reference_t<Args>>...,
  Args...>
{
  using base = variant_traits<
    std::reference_wrapper<std::remove_reference_t<Args>>...,
    Args...>;
  template<typename T>
  static void complete(typename base::storage_type&& storage, T& t) noexcept {
    if (!base::maybe_fail(std::move(storage), t)) {
      const auto ptr = std::get_if<std::reference_wrapper<std::remove_reference_t<Args>>...>(&storage);
      t.set_value(std::forward<Args...>(ptr->get()));
    }
  }
  static void store(typename base::storage_type& storage, Args&&... args) noexcept {
    //  Turns rvalue references into lvalue references which is necessary to construct a reference_wrapper in the base
    base::store(storage, args...);
  }
};

template<>
struct traits<> {
  using storage_type = std::optional<std::exception_ptr>;
  using value_completion_signature = ::stdexec::set_value_t();
  template<typename T>
  static void complete(const storage_type& storage, T& t) noexcept {
    if (storage) {
      t.set_error(std::move(*storage));
    } else {
      t.set_value();
    }
  }
  static void store(const storage_type&) noexcept {}
};

template<typename Derived, typename... Args>
struct promise_base {
  constexpr void return_value(Args&&... args) noexcept {
    static_cast<Derived&>(*this).return_impl(std::forward<Args>(args)...);
  }
};

template<typename Derived>
struct promise_base<Derived> {
  constexpr void return_void() noexcept {
    static_cast<Derived&>(*this).return_impl();
  }
};

template<typename... Args>
class sender {
  struct operation_state_base_;
  using traits_ = coroutine_sender::traits<Args...>;
public:
  struct promise_type : promise_base<promise_type, Args...> {
    constexpr auto get_return_object() noexcept {
      return sender(*this);
    }
    constexpr static sender get_return_object_on_allocation_failure() noexcept {
      return {};
    }
    constexpr std::suspend_always initial_suspend() noexcept {
      return {};
    }
    constexpr std::suspend_never final_suspend() noexcept {
      STDEXEC_ASSERT(op_);
      traits_::complete(std::move(storage_), *op_);
      return {};
    }
    void unhandled_exception() noexcept {
      storage_ = std::current_exception();
    }
    constexpr auto unhandled_stopped() noexcept {
      op_->set_stopped(std::coroutine_handle<promise_type>::from_promise(*this));
      return std::noop_coroutine();
    }
    template<::stdexec::sender Sender>
    constexpr auto await_transform(Sender&& sender) /*noexcept(????)*/ {
      return ::stdexec::as_awaitable(std::forward<Sender>(sender), *this);
    }
    constexpr void return_impl(Args&&... args) noexcept {
      traits_::store(storage_, std::forward<Args>(args)...);
    }
    operation_state_base_* op_{nullptr};
    typename traits_::storage_type storage_;
  };
  sender() = default;
  constexpr explicit sender(promise_type& promise) noexcept
    : promise_(&promise)
  {}
  using sender_concept = ::stdexec::sender_t;
  template<typename Env>
  consteval ::stdexec::completion_signatures<
    typename traits_::value_completion_signature,
    ::stdexec::set_error_t(std::exception_ptr),
    ::stdexec::set_stopped_t()> get_completion_signatures(const Env&) && noexcept
  {
    return {};
  }
private:
  struct operation_state_base_ {
    virtual void set_value(Args&&...) noexcept = 0;
    virtual void set_error(std::exception_ptr) noexcept = 0;
    virtual void set_stopped(std::coroutine_handle<promise_type>) noexcept = 0;
  };
  template<::stdexec::receiver Receiver>
  struct operation_state_ : private operation_state_base_ {
    constexpr explicit operation_state_(promise_type* promise, Receiver r) noexcept
      : promise_(promise),
        r_(std::move(r))
    {}
    operation_state_(operation_state_&&) = delete;
    operation_state_& operator=(operation_state_&&) = delete;
    constexpr ~operation_state_() noexcept {
      if (promise_) {
        std::coroutine_handle<promise_type>::from_promise(*promise_).destroy();
      }
    }
    void start() & noexcept {
      if (promise_) {
        const auto handle = std::coroutine_handle<promise_type>::from_promise(
          *promise_);
        STDEXEC_ASSERT(!promise_->op_);
        promise_->op_ = this;
        promise_ = nullptr;
        handle.resume();
      } else {
        try {
          ::stdexec::set_error(
            std::move(r_),
            std::make_exception_ptr(std::bad_alloc{}));
        } catch (...) {
          ::stdexec::set_error(
            std::move(r_),
            std::current_exception());
        }
      }
    }
  private:
    virtual void set_value(Args&&... args) noexcept override {
      ::stdexec::set_value(std::move(r_), std::forward<Args>(args)...);
    }
    virtual void set_error(std::exception_ptr ex) noexcept override {
      ::stdexec::set_error(std::move(r_), std::move(ex));
    }
    virtual void set_stopped(const std::coroutine_handle<promise_type> handle) noexcept override {
      STDEXEC_ASSERT(!promise_);
      //  Ensures the coroutine frame will be cleaned up by the operation state's lifetime ending
      promise_ = &handle.promise();
      ::stdexec::set_stopped(std::move(r_));
    }
    promise_type* promise_;
    Receiver r_;
  };
public:
  template<typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        sender,
        ::stdexec::env_of_t<Receiver>>>
  constexpr auto connect(Receiver r) && noexcept {
    return operation_state_<Receiver>(promise_, std::move(r));
  }
private:
  promise_type* promise_{nullptr};
};

}

template<typename T>
using coroutine_sender = std::conditional_t<
  std::is_same_v<T, void>,
  detail::coroutine_sender::sender<>,
  detail::coroutine_sender::sender<T>>;

} // namespace exec
