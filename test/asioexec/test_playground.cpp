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

#include <asioexec/asio_config.hpp>

#include <catch2/catch.hpp>

#include <stdexec/execution.hpp>
#include "../test_common/receivers.hpp"

#include <atomic>
#include <cassert>
#include <chrono>
#include <cstddef>
#include <exception>
#include <functional>
#include <memory>
#include <optional>
#include <ranges>
#include <stdexcept>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <vector>

#include <iostream>

using namespace asioexec;

template<typename T>
struct transform_signature;
template<typename... Args>
struct transform_signature<void(Args...)> {
  using type = ::stdexec::set_value_t(Args...);
};

template<typename... Signatures>
using transform_signatures = ::stdexec::completion_signatures<
  typename transform_signature<Signatures>::type...,
  ::stdexec::set_error_t(std::exception_ptr),
  ::stdexec::set_stopped_t()>;

//template<typename Signatures, typename Initiation, typename... Args>
//struct completion_signatures;
//template<typename... Signatures, typename Initiation, typename... Args>
//struct completion_signatures<
//  ::stdexec::completion_signatures<Signatures...>,
//  Initiation,
//  Args...>
//{
//  using type = ::stdexec::completion_signatures<
//    Signatures...,
//    ::stdexec::set_error_t(std::exception_ptr)>;
//};
//template<typename... Signatures, typename Initiation, typename... Args>
//  requires std::is_nothrow_invocable_v<Initiation, Args...>
//struct completion_signatures<
//  ::stdexec::completion_signatures<Signatures...>,
//  Initiation,
//  Args...>
//{
//  using type = ::stdexec::completion_signatures<
//    Signatures...>;
//};

//template<typename Receiver, typename Initiation>
//struct operation {
//  Receiver r_;
//  Initiation init_;
//  using operation_state_concept = ::stdexec::operation_state_t;
//  void start() & noexcept {
//    std::invoke(
//      std::move(init_),
//      [this](auto&&... args) noexcept {
//        ::stdexec::set_value(std::move(r_), std::forward<decltype(args)>(args)...);
//      });
//  }
//};

template<typename, typename>
struct operation;

using on_stop_request = std::move_only_function<void () && noexcept>;

template<typename Token>
using stop_callback_t = ::stdexec::stop_callback_for_t<
  Token,
  on_stop_request>;

template<typename H>
class cancellation_wrapper {
  H h_;
public:
  template<typename... Args>
    requires std::is_constructible_v<H, Args...>
  constexpr explicit cancellation_wrapper(H*& h, Args&&... args) noexcept(
    std::is_nothrow_constructible_v<H, Args...>)
    : h_(std::forward<Args>(args)...)
  {
    h = std::addressof(h_);
  }
  cancellation_wrapper(const cancellation_wrapper&) = delete;
  cancellation_wrapper& operator=(const cancellation_wrapper&) = delete;
  template<typename Self>
  constexpr void operator()(this Self&& self) noexcept {
    std::invoke(std::forward<Self>(self).h_, asio_impl::cancellation_type::all);
  }
};

struct initiating {};
struct initiated {};

template<typename Token>
struct cancellation_signal {
  std::variant<
    initiating,
    initiated,
    on_stop_request,
    stop_callback_t<Token>> state_;
  std::mutex m_;
};

template<typename Receiver, typename Initiation>
struct completion_handler {
  constexpr completion_handler(operation<Receiver, Initiation>* self) noexcept
    : self_(self) {}
  constexpr completion_handler(completion_handler&& other) noexcept
    : self_(other.self_)
  {
    other.self_ = nullptr;
  }
  constexpr ~completion_handler() noexcept {
    if (self_ && self_->state_->should_complete_()) {
      self_->complete_();
    }
  }
  template<typename... Ts>
  constexpr void operator()(Ts&&... ts) noexcept {
    self_->state_->outstanding_.store(-1, std::memory_order_relaxed);
    self_->get_cancellation_slot_().clear();
    //self_->callback_.reset();
    //self_->state_->release_();
    auto&& r = self_->r_;
    self_ = nullptr;
    ::stdexec::set_value(std::move(r), std::forward<Ts>(ts)...);
  }
  //constexpr auto get_cancellation_slot() const noexcept {
  //  return cancellation_slot<
  //    ::stdexec::stop_token_of_t<
  //      ::stdexec::env_of_t<
  //        Receiver>>>{&self_->callback_};
  //}
  operation<Receiver, Initiation>* self_;
};

template<typename Executor, typename Receiver, typename Initiation>
struct executor {
  operation<Receiver, Initiation>& self_;
  Executor ex_;
  constexpr bool operator==(const executor& other) const noexcept {
    return (ex_ == other.ex_) && (&self_ == &other.self_);
  }
  bool operator!=(const executor& other) const = default;
  template<typename F>
  void execute(F f) const noexcept {
    const auto ptr = self_.state_;
    ptr->outstanding_.fetch_add(1, std::memory_order_relaxed);
    try {
      ex_.execute([&self = self_, f = std::move(f), ptr]() mutable noexcept {
        ptr->outstanding_.fetch_add(1, std::memory_order_relaxed);
        try {
          std::move(f)();
        } catch (...) {
          ptr->set_exception_();
        }
        if (ptr->should_complete_()) {
          self.complete_();
        }
      });
    } catch (...) {
      ptr->set_exception_();
    }
    if (ptr->should_complete_()) {
      self_.complete_();
    }
  }
  template<typename... Args>
    requires requires (const Executor& ex) {
      asio_impl::require(
        ex,
        std::declval<Args>()...);
    }
  decltype(auto) require(Args&&... args) const {
    auto ex = asio_impl::require(
      ex_,
      std::forward<Args>(args)...);
    return executor<decltype(ex), Receiver, Initiation>{
      self_,
      std::move(ex)};
  }
};

//struct on_stop_request {
//  void operator()() && noexcept {
//    signal_.emit(asio_impl::cancellation_type::all);
//  }
//  asio_impl::cancellation_signal& signal_;
//};

struct completed {};

template<typename Token>
struct state {
  //  One for the completion handler, one for start
  std::atomic<std::size_t> outstanding_{2};
  std::atomic<bool> set_ex_{false};
  std::exception_ptr ex_;
  std::variant<
    initiating,
    initiated,
    on_stop_request,
    stop_callback_t<Token>> cancellation_;
  std::mutex m_;
  //std::mutex m_;
  //asio_impl::cancellation_signal signal_;
  //using callback_type_ = 
  //  //  std::
  //  ::stdexec::stop_callback_for_t<
  //    Token,
  //    on_stop_request>;
  //std::variant<
  //  std::monostate,
  //  callback_type_,
  //  completed> callback_;
  void set_exception_() noexcept {
    if (!set_ex_.exchange(true, std::memory_order_relaxed)) {
      ex_ = std::current_exception();
    }
  }
  bool should_complete_() noexcept {
    return outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }
  //void release_() noexcept {
  //  const std::lock_guard l(m_);
  //  callback_ = completed{};
  //}
  //template<typename F>
  //void acquire_(F f) noexcept {
  //  const std::lock_guard l(m_);
  //  if (std::holds_alternative<completed>(callback_)) {
  //    return;
  //  }
  //  callback_.template emplace<callback_type_>(
  //    std::invoke(std::move(f)),
  //    on_stop_request{signal_});
  //}
};

template<typename Token>
struct cancellation_slot {
  Token token_;
  //std::optional<stop_callback_t<Token>>* callback_;
  std::shared_ptr<state<Token>> state_;
  bool operator==(const cancellation_slot&) const = default;
  bool operator!=(const cancellation_slot&) const = default;
  constexpr bool is_initiated_(const std::lock_guard<std::mutex>&) const noexcept {
    return
      std::holds_alternative<initiated>(state_->cancellation_) ||
      std::holds_alternative<stop_callback_t<Token>>(state_->cancellation_);
  }
  static constexpr bool is_connected() noexcept {
    return true;
  }
  constexpr bool has_handler() const noexcept {
    //  TOOD
    //return bool(*callback_);
    return false;
  }
  template<typename H>
  constexpr decltype(auto) assign(H&& h) {
    return emplace<std::remove_cvref_t<H>>(std::forward<H>(h));
  }
  template<typename H, typename... Args>
  constexpr H& emplace(Args&&... args) {
    using handler = std::remove_cvref_t<H>;
    using wrapper = cancellation_wrapper<handler>;
    handler* retr;
    on_stop_request f(
      std::in_place_type<wrapper>,
      retr,
      std::forward<Args>(args)...);
    {
      const std::lock_guard l(state_->m_);
      if (is_initiated_(l)) {
        state_->cancellation_.template emplace<stop_callback_t<Token>>(
          token_,
          std::move(f));
        assert(is_initiated_(l));
      } else {
        state_->cancellation_ = std::move(f);
        assert(!is_initiated_(l));
      }
    }
    //callback_->emplace(
    //  token_,
    //  std::move(f));
    return *retr;
  }
  constexpr void clear() noexcept {
    const std::lock_guard l(state_->m_);
    if (is_initiated_(l)) {
      state_->cancellation_ = initiated{};
    } else {
      state_->cancellation_ = initiating{};
    }
    //callback_->reset();
  }
};

template<typename Receiver, typename Initiation>
struct operation {
                           // std::
  using stop_token_type_ = ::stdexec::stop_token_of_t<
    ::stdexec::env_of_t<Receiver>>;
  using state_type_ = state<stop_token_type_>;
  Receiver r_;
  Initiation init_;
  std::shared_ptr<state_type_> state_{std::make_shared<state_type_>()};
  //std::shared_ptr<state> state_{std::make_shared<state>()};
  //std::optional<
  //  stop_callback_t<
  //    ::stdexec::stop_token_of_t<
  //      ::stdexec::env_of_t<
  //        Receiver>>>> callback_;
  //asio_impl::cancellation_signal signal_;
  //std::optional<
  //  //  std::
  //  ::stdexec::stop_callback_for_t<
  //    //  std::
  //    ::stdexec::stop_token_of_t<
  //      ::stdexec::env_of_t<
  //        Receiver>>,
  //    on_stop_request>> callback_;
  using operation_state_concept = ::stdexec::operation_state_t;
  constexpr auto get_cancellation_slot_() noexcept {
    return cancellation_slot<stop_token_type_>{
      ::stdexec::get_stop_token(
        ::stdexec::get_env(r_)),
      state_};
  }
  void start() & noexcept {
    //  TODO: Add this line and consequences thereof to slides
    const auto ptr = state_;
    try {
      std::invoke(
        std::move(init_),
        completion_handler<Receiver, Initiation>{this});
    } catch (...) {
      ptr->set_exception_();
    }
    if (ptr->should_complete_()) {
      complete_();
      return;
    }
    const std::lock_guard l(ptr->m_);
    if (const auto f = std::get_if<on_stop_request>(&ptr->cancellation_); f) {
      auto local = std::move(*f);
      ptr->cancellation_.template emplace<stop_callback_t<stop_token_type_>>(
        ::stdexec::get_stop_token(
          ::stdexec::get_env(r_)),
        std::move(local));
    } else {
      ptr->cancellation_ = initiated{};
    }
    //  TODO: Setup cancellation
    //callback_.emplace(
    //  //  std::
    //  ::stdexec::get_stop_token(
    //    ::stdexec::get_env(r_)),
    //  on_stop_request{signal_});
    //ptr->acquire_([&]() noexcept {
    //  return ::stdexec::get_stop_token(
    //    ::stdexec::get_env(r_));
    //});
  }
  void complete_() noexcept {
    get_cancellation_slot_().clear();
    //state_->release_();
    //callback_.reset();
    if (state_->ex_) {
      ::stdexec::set_error(std::move(r_), std::move(state_->ex_));
      return;
    }
    ::stdexec::set_stopped(std::move(r_));
  }
};

template<typename Signatures, typename Initiation>
struct sender {
  using sender_concept = ::stdexec::sender_t;
  Initiation init_;
  template<typename Self, typename Env>
    requires
      std::is_constructible_v<Initiation, decltype(std::forward_like<Self>(std::declval<Initiation&>()))>
  Signatures get_completion_signatures(this Self&&, const Env&) noexcept {
    return {};
  }
  template<typename Self, typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        sender,
        ::stdexec::env_of_t<Receiver>>>
  constexpr auto connect(this Self&& self, Receiver r) {
    return operation<Receiver, Initiation>{std::move(r), std::forward<Self>(self).init_};
  }
};

struct completion_token_t {};

inline constexpr completion_token_t completion_token;

namespace ASIOEXEC_ASIO_NAMESPACE {

template <typename... Signatures>
struct async_result<completion_token_t, Signatures...> {
  template <typename Initiation, typename... Args>
  static constexpr auto initiate(
    Initiation init,
    const completion_token_t&,
    Args... args)
  {
    auto f = [init = std::move(init), ...args = std::move(args)](this auto&& self, auto h) {
      std::invoke(
        std::forward_like<decltype(self)>(init),
        std::move(h),
        std::forward_like<decltype(self)>(args)...);
    };
    return sender<
      transform_signatures<Signatures...>,
      decltype(f)>{std::move(f)};
  }
};

template<typename Receiver, typename Initiation, typename Executor>
struct associated_executor<completion_handler<Receiver, Initiation>, Executor> {
  using type = ::executor<Executor, Receiver, Initiation>;
  static constexpr type get(const completion_handler<Receiver, Initiation>& h, Executor ex = Executor()) noexcept {
    return type{*h.self_, std::move(ex)};
  }
};

template<typename Receiver, typename Initiation, typename CancellationSlot>
struct associated_cancellation_slot<completion_handler<Receiver, Initiation>, CancellationSlot> {
  //using type = asio_impl::cancellation_slot;
  using type = ::cancellation_slot<
    ::stdexec::stop_token_of_t<
      ::stdexec::env_of_t<
        Receiver>>>;
  static constexpr type get(const completion_handler<Receiver, Initiation>& h, CancellationSlot slot = CancellationSlot()) noexcept {
    return h.self_->get_cancellation_slot_();
    //return {
    //  ::stdexec::get_stop_token(
    //    ::stdexec::get_env(h.self_->r_)),
    //  h.self_->state_};
      //&h.self_->callback_};
    //return h.self_->state_->signal_.slot();
    //return h.self_->signal_.slot();
  }
};

}

namespace {

  template<typename Executor, typename Range, typename Function, typename CompletionToken>
  decltype(auto) async_for_each(const Executor& original_ex, Range& r, Function f, CompletionToken&& token) {
    return asio_impl::async_initiate<CompletionToken, void()>(
      [original_ex, &r, f = std::move(f)](auto h) mutable {
        const auto ex = asio_impl::require(
          asio_impl::get_associated_executor(h, original_ex),
          asio::execution::blocking.never);
        ex.execute(
          [ex, &r, begin = std::ranges::begin(r), f = std::move(f), h = std::move(h)](this auto&& self) {
            if (begin == std::ranges::end(r)) {
              std::move(h)();
              return;
            } else {
              f(*begin++);
              ex.execute(std::move(self));
            }
          });
      },
      token);
  }

  template<typename Executor, typename A, typename B, typename Function, typename CompletionToken>
  decltype(auto) async_for_each_both(const Executor& original_ex, A& a, B& b, Function f, CompletionToken&& token) {
    return asio_impl::async_initiate<CompletionToken, void()>(
      [original_ex, &a, &b, f = std::move(f)](auto h) mutable {
        const auto ex = asio_impl::get_associated_executor(h, original_ex);
        struct state {
          decltype(h) handler;
          std::atomic<std::size_t> completed{0};
        };
        auto wrapped = [ptr = std::make_shared<state>(std::move(h))]() mutable {
          if (ptr->completed.fetch_add(1, std::memory_order_acq_rel) == 1) {
            std::move(ptr->handler)();
          }
        };
        ::async_for_each(ex, a, f, wrapped);
        ::async_for_each(ex, b, std::move(f), std::move(wrapped));
      },
      token);
  }

  TEST_CASE(
    "Integration with post",
    "[asioexec][use_sender]") {
    asio_impl::io_context ctx;
    auto sender = asio_impl::post(ctx, completion_token);
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_void_receiver{});
    ::stdexec::start(op);
    CHECK(ctx.poll() == 1);
  }

  TEST_CASE(
    "async_for_each",
    "[asioexec][use_sender]") {
    asio_impl::io_context ctx;
    std::size_t invoked = 0;
    const auto f = [&, expected = 1](const int i) mutable noexcept {
      ++invoked;
      CHECK(i == expected);
      ++expected;
    };
    std::vector<int> v;
    {
      auto sender = async_for_each(
        ctx.get_executor(),
        v,
        f,
        completion_token);
      auto op = ::stdexec::connect(
        std::move(sender),
        expect_void_receiver{});
      ::stdexec::start(op);
      CHECK(invoked == 0);
      CHECK(ctx.poll() == 1);
      CHECK(invoked == 0);
    }
    v.push_back(1);
    v.push_back(2);
    ctx.restart();
    {
      auto sender = async_for_each(
        ctx.get_executor(),
        v,
        f,
        completion_token);
      auto op = ::stdexec::connect(
        std::move(sender),
        expect_void_receiver{});
      ::stdexec::start(op);
      CHECK(invoked == 0);
      CHECK(ctx.poll() != 0);
      CHECK(invoked == 2);
    }
    ctx.restart();
    {
      auto sender = async_for_each(
        ctx.get_executor(),
        v,
        [](auto&&...) { throw std::logic_error("Test"); },
        completion_token);
      auto op = ::stdexec::connect(
        std::move(sender),
        expect_error_receiver{});
      ::stdexec::start(op);
      CHECK(ctx.poll() != 0);
    }
  }

  template <typename Receiver>
  class connect_shared_receiver {
    Receiver r_;
    std::shared_ptr<void>& ptr_;

    template <typename Tag, typename... Args>
    void complete_(const Tag& tag, Args&&... args) noexcept {
      CHECK(ptr_);
      CHECK(ptr_.use_count() == 1);
      tag(std::move(r_), std::forward<Args>(args)...);
      ptr_.reset();
    }
   public:
    using receiver_concept = ::stdexec::receiver_t;

    template <typename T>
      requires std::constructible_from<Receiver, T>
    constexpr explicit connect_shared_receiver(T&& t, std::shared_ptr<void>& ptr) noexcept
      : r_(std::forward<T>(t))
      , ptr_(ptr) {
    }

    constexpr void set_stopped() && noexcept
      requires ::stdexec::receiver_of<
        Receiver,
        ::stdexec::completion_signatures<::stdexec::set_stopped_t()>
      >
    {
      complete_(::stdexec::set_stopped);
    }

    template <typename T>
      requires ::stdexec::receiver_of<
        Receiver,
        ::stdexec::completion_signatures<::stdexec::set_error_t(T)>
      >
    constexpr void set_error(T&& t) && noexcept {
      complete_(::stdexec::set_error, std::forward<T>(t));
    }

    template <typename... Args>
      requires ::stdexec::receiver_of<
        Receiver,
        ::stdexec::completion_signatures<::stdexec::set_value_t(Args...)>
      >
    constexpr void set_value(Args&&... args) && noexcept {
      complete_(::stdexec::set_value, std::forward<Args>(args)...);
    }

    constexpr decltype(auto) get_env() const noexcept {
      return ::stdexec::get_env(r_);
    }
  };

  template <typename Sender, typename Receiver>
  class connect_shared_operation_state {
    using receiver_ = connect_shared_receiver<std::remove_cvref_t<Receiver>>;
    std::shared_ptr<void> self_;
    ::stdexec::connect_result_t<Sender, receiver_> op_;
   public:
    constexpr explicit connect_shared_operation_state(Sender&& s, Receiver&& r)
      : op_(
          ::stdexec::connect(
            std::forward<Sender>(s),
            receiver_(std::forward<Receiver>(r), self_))) {
    }

    void start(std::shared_ptr<connect_shared_operation_state>&& ptr) & noexcept {
      CHECK(ptr.get() == this);
      CHECK(ptr.use_count() == 1);
      self_ = std::move(ptr);
      ::stdexec::start(op_);
    }
  };

  template <typename Sender, typename Receiver>
  auto connect_shared(Sender&& sender, Receiver&& receiver) {
    return std::make_shared<connect_shared_operation_state<Sender, Receiver>>(
      std::forward<Sender>(sender), std::forward<Receiver>(receiver));
  }

  template <typename Sender, typename Receiver>
  void
    start_shared(std::shared_ptr<connect_shared_operation_state<Sender, Receiver>>&& ptr) noexcept {
    REQUIRE(ptr);
    auto&& state = *ptr;
    state.start(std::move(ptr));
  }

  TEST_CASE(
    "async_for_each_both",
    "[asioexec][use_sender]") {
    asio_impl::io_context ctx;
    std::size_t invoked = 0;
    const auto f = [&](int) mutable noexcept {
      ++invoked;
    };
    std::vector<int> a{5};
    std::vector<int> b{6, 7};
    {
      auto sender = async_for_each_both(
        ctx.get_executor(),
        a,
        b,
        f,
        completion_token);
      auto ptr = connect_shared(
        std::move(sender),
        expect_void_receiver{});
      start_shared(std::move(ptr));
      CHECK(invoked == 0);
      CHECK(ctx.poll() != 0);
      CHECK(invoked == 3);
    }
  }

  TEST_CASE(
    "async_for_each_both exception",
    "[asioexec][use_sender]") {
    asio_impl::io_context ctx;
    const auto f = [](auto&&...) {
      throw std::logic_error("Test");
    };
    std::vector<int> a{5};
    std::vector<int> b{6};
    {
      auto sender = async_for_each_both(
        ctx.get_executor(),
        a,
        b,
        f,
        completion_token);
      bool error = false;
      auto ptr = connect_shared(
        std::move(sender) | ::stdexec::upon_error([&](auto&&) noexcept {
          error = true;
        }),
        expect_void_receiver{});
      start_shared(std::move(ptr));
      CHECK(ctx.poll_one());
      CHECK(!error);
      CHECK(ctx.poll_one());
      CHECK(error);
    }
  }

  TEST_CASE(
    "Cancellation",
    "[asioexec][use_sender]") {
    ::stdexec::inplace_stop_source source;
    asio_impl::io_context ctx;
    asio_impl::system_timer timer(ctx);
    timer.expires_after(std::chrono::years(1));
    auto sender = timer.async_wait(completion_token);
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_value_receiver(
        env_tag{},
        ::stdexec::prop(
          ::stdexec::get_stop_token,
          source.get_token()),
        make_error_code(asio_impl::error::operation_aborted)));
    source.request_stop();
    ::stdexec::start(op);
    ctx.run();
  }

} // namespace
