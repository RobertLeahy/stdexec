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

#include <exec/coroutine_sender.hpp>

#include <catch2/catch.hpp>
#include <exec/single_thread_context.hpp>
#include <stdexec/execution.hpp>

#include <optional>
#include <thread>
#include <type_traits>
#include <utility>

#include <exec/unless_stop_requested.hpp>
#include "../test_common/receivers.hpp"

using namespace exec;

namespace {

TEST_CASE("A non-void coroutine which co_returns immediately can be connected and started", "[coroutine_sender]") {
  {
    bool invoked = false;
    const auto f = [&]() -> coroutine_sender<int> {
      CHECK(!invoked);
      invoked = true;
      co_return 5;
    };
    auto sender = f();
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_value_receiver(5));
    CHECK(!invoked);
    ::stdexec::start(op);
    CHECK(invoked);
  }
  {
    bool invoked = false;
    const auto f = [&]() -> coroutine_sender<
      ::stdexec::completion_signatures<::stdexec::set_value_t(int)>>
    {
      CHECK(!invoked);
      invoked = true;
      co_return 5;
    };
    auto sender = f();
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_value_receiver(5));
    CHECK(!invoked);
    ::stdexec::start(op);
    CHECK(invoked);
  }
}

TEST_CASE("A void coroutine which co_returns immediately can be connected and started", "[coroutine_sender]") {
  {
    bool invoked = false;
    const auto f = [&]() -> coroutine_sender<void> {
      invoked = true;
      co_return coroutine_sender_void;
    };
    auto sender = f();
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_void_receiver{});
    CHECK(!invoked);
    ::stdexec::start(op);
    CHECK(invoked);
  }
  {
    bool invoked = false;
    const auto f = [&]() -> coroutine_sender<
      ::stdexec::completion_signatures<::stdexec::set_value_t()>>
    {
      invoked = true;
      co_return coroutine_sender_void;
    };
    auto sender = f();
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_void_receiver{});
    CHECK(!invoked);
    ::stdexec::start(op);
    CHECK(invoked);
  }
}

TEST_CASE("A non-void coroutine which co_returns an lvalue reference can be connected and started", "[coroutine_sender]") {
  int i = 5;
  bool invoked = false;
  bool completed = false;
  const auto f = [&]() -> coroutine_sender<int&> {
    invoked = true;
    co_return i;
  };
  auto sender = f();
  auto op = ::stdexec::connect(
    std::move(sender),
    make_fun_receiver([&](auto&& arg) noexcept {
      CHECK(!completed);
      completed = true;
      static_assert(std::is_same_v<int&, decltype(arg)>);
      CHECK(arg == i);
      CHECK(&arg == &i);
    }));
  CHECK(!invoked);
  CHECK(!completed);
  ::stdexec::start(op);
  CHECK(invoked);
  CHECK(completed);
}

//TEST_CASE("A non-void coroutine which co_returns an rvalue reference can be connected and started", "[coroutine_sender]") {
//  int i = 5;
//  bool invoked = false;
//  bool completed = false;
//  const auto f = [&]() -> coroutine_sender<int&&> {
//    invoked = true;
//    co_return std::move(i);
//  };
//  auto sender = f();
//  auto op = ::stdexec::connect(
//    std::move(sender),
//    make_fun_receiver([&](auto&& arg) noexcept {
//      CHECK(!completed);
//      completed = true;
//      static_assert(std::is_same_v<int&&, decltype(arg)>);
//      CHECK(arg == i);
//      CHECK(&arg == &i);
//    }));
//  CHECK(!invoked);
//  CHECK(!completed);
//  ::stdexec::start(op);
//  CHECK(invoked);
//  CHECK(completed);
//}

TEST_CASE("When the coroutine sender type has may set_value completion signatures the body of the coroutine may use one of them", "[coroutine_sender]") {
  using completion_signatures = ::stdexec::completion_signatures<
    ::stdexec::set_value_t(),
    ::stdexec::set_value_t(int),
    ::stdexec::set_value_t(float),
    ::stdexec::set_value_t(bool, double)>;
  using sender_type = coroutine_sender<completion_signatures>;
  {
    bool invoked = false;
    const auto f = [&]() -> sender_type {
      CHECK(!invoked);
      invoked = true;
      co_return coroutine_sender_void;
    };
    auto sender = f();
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_void_receiver{});
    CHECK(!invoked);
    ::stdexec::start(op);
    CHECK(invoked);
  }
  {
    bool invoked = false;
    const auto f = [&]() -> sender_type {
      CHECK(!invoked);
      invoked = true;
      co_return 5;
    };
    auto sender = f();
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_value_receiver(5));
    CHECK(!invoked);
    ::stdexec::start(op);
    CHECK(invoked);
  }
  {
    bool invoked = false;
    const auto f = [&]() -> sender_type {
      CHECK(!invoked);
      invoked = true;
      co_return 5.5f;
    };
    auto sender = f();
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_value_receiver(5.5f));
    CHECK(!invoked);
    ::stdexec::start(op);
    CHECK(invoked);
  }
  {
    const auto receiver = make_fun_receiver([&]<typename... Args>(Args... args) noexcept {
      if constexpr (sizeof...(Args) == 2) {
        static_assert(
          std::is_same_v<
            std::pair<Args...>,
            std::pair<bool, double>>);
        const std::pair p(args...);
        CHECK(p.first);
        CHECK(p.second == 5.5);
      } else {
        //  This just induces Catch to print the actual cardinality
        CHECK(sizeof...(Args) == 2);
      }
    });
    bool invoked = false;
    const auto f = [&]() -> sender_type {
      CHECK(!invoked);
      invoked = true;
      co_return std::pair(true, 5.5);
    };
    auto sender = f();
    auto op = ::stdexec::connect(
      std::move(sender),
      receiver);
    CHECK(!invoked);
    ::stdexec::start(op);
    CHECK(invoked);
  }
}

TEST_CASE("When the coroutine sender type has may set_value completion signatures the body of the coroutine may use any of them", "[coroutine_sender]") {
  using completion_signatures = ::stdexec::completion_signatures<
    ::stdexec::set_value_t(),
    ::stdexec::set_value_t(int),
    ::stdexec::set_value_t(float),
    ::stdexec::set_value_t(bool, double)>;
  using sender_type = coroutine_sender<completion_signatures>;
  const auto f = [&](const int i) -> sender_type {
    if (i == 0) {
      co_return coroutine_sender_void;
    } else if (i == 1) {
      co_return 5;
    } else if (i == 2) {
      co_return 5.5f;
    } else {
      co_return std::pair(true, 5.5);
    }
  };
  for (int i = 0; i < 4; ++i) {
    auto sender = f(i);
    bool invoked = false;
    const auto receiver = make_fun_receiver([&]<typename... Args>(Args... args) noexcept {
      invoked = true;
      if constexpr (sizeof...(Args) == 2) {
        CHECK(i == 3);
        [&](const bool b, const double d) noexcept {
          CHECK(b);
          CHECK(d == 5.5);
        }(args...);
      } else if constexpr (sizeof...(Args) == 1) {
        [&]<typename T>(const T t) noexcept {
          if constexpr (std::is_same_v<T, int>) {
            CHECK(i == 1);
            CHECK(t == 5);
          } else {
            static_assert(std::is_same_v<T, float>);
            CHECK(i == 2);
            CHECK(t == 5.5f);
          }
        }(args...);
      } else {
        static_assert(!sizeof...(Args));
        CHECK(i == 0);
      }
    });
    auto op = ::stdexec::connect(std::move(sender), receiver);
    CHECK(!invoked);
    ::stdexec::start(op);
    CHECK(invoked);
  }
}

TEST_CASE("Abandoning the sender without connecting it cleans up the coroutine frame", "[coroutine_sender]") {
  const auto f = [&]() -> coroutine_sender<int> {
    co_return 5;
  };
  auto sender = f();
  //  Failure mode is LSan
}

TEST_CASE("Abandoning the operation state without starting it cleans up the coroutine frame", "[coroutine_sender]") {
  const auto f = [&]() -> coroutine_sender<int> {
    co_return 5;
  };
  auto sender = f();
  auto op = ::stdexec::connect(
    std::move(sender),
    empty_recv::recv_int{});
  //  Failure mode is LSan
}

TEST_CASE("Senders can be co_awaited in the body of the coroutine", "[coroutine_sender]") {
  bool invoked = false;
  const auto f = [&]() -> coroutine_sender<int> {
    invoked = true;
    co_return co_await ::stdexec::just(5);
  };
  auto sender = f();
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_value_receiver(5));
  CHECK(!invoked);
  ::stdexec::start(op);
  CHECK(invoked);
}

TEST_CASE("Stopped simply ends the coroutine", "[coroutine_sender]") {
  bool invoked = false;
  bool reached = false;
  const auto f = [&]() -> coroutine_sender<int> {
    invoked = true;
    co_await ::stdexec::just_stopped();
    reached = true;
    co_return 5;
  };
  auto sender = f();
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_stopped_receiver{});
  CHECK(!invoked);
  CHECK(!reached);
  ::stdexec::start(op);
  CHECK(invoked);
  CHECK(!reached);
}

TEST_CASE("Permits a coroutine to move between execution contexts", "[coroutine_sender]") {
  bool completed = false;
  bool started = false;
  std::optional<single_thread_context> ctx(std::in_place);
  const auto expected = ctx->get_thread_id();
  const auto f = [&, sch = ctx->get_scheduler()]() -> coroutine_sender<void> {
    started = true;
    co_await ::stdexec::schedule(sch);
    CHECK(std::this_thread::get_id() == expected);
    completed = true;
    co_return coroutine_sender_void;
  };
  auto op = ::stdexec::connect(f(), expect_void_receiver{});
  CHECK(!started);
  CHECK(!completed);
  ::stdexec::start(op);
  CHECK(started);
  ctx.reset();
  CHECK(completed);
}

TEST_CASE("Senders co_awaited in the coroutine receive stop requests from the appropriate inplace_stop_source", "[coroutine_sender]") {
  bool completed = false;
  bool started = false;
  const auto f = [&]() -> coroutine_sender<void> {
    started = true;
    co_await ::exec::unless_stop_requested(::stdexec::just());
    completed = true;
    co_return coroutine_sender_void;
  };
  ::stdexec::inplace_stop_source source;
  const ::stdexec::prop env(::stdexec::get_stop_token, source.get_token());
  {
    auto op = ::stdexec::connect(f(), expect_void_receiver(env));
    CHECK(!started);
    CHECK(!completed);
    ::stdexec::start(op);
    CHECK(started);
    CHECK(completed);
  }
  completed = false;
  started = false;
  source.request_stop();
  {
    auto op = ::stdexec::connect(f(), expect_stopped_receiver(env));
    CHECK(!started);
    CHECK(!completed);
    ::stdexec::start(op);
    CHECK(started);
    CHECK(!completed);
  }
}

TEST_CASE("Senders co_awaited in the coroutine receive stop requests from the appropriate stop source which is not an inplace_stop_source", "[coroutine_sender]") {
  bool completed = false;
  bool started = false;
  const auto f = [&]() -> coroutine_sender<void> {
    started = true;
    co_await ::exec::unless_stop_requested(::stdexec::just());
    completed = true;
    co_return coroutine_sender_void;
  };
  std::stop_source source;
  const ::stdexec::prop env(::stdexec::get_stop_token, source.get_token());
  {
    auto op = ::stdexec::connect(f(), expect_void_receiver(env));
    CHECK(!started);
    CHECK(!completed);
    ::stdexec::start(op);
    CHECK(started);
    CHECK(completed);
  }
  completed = false;
  started = false;
  source.request_stop();
  {
    auto op = ::stdexec::connect(f(), expect_stopped_receiver(env));
    CHECK(!started);
    CHECK(!completed);
    ::stdexec::start(op);
    CHECK(started);
    CHECK(!completed);
  }
}

} // unnamed namespace
