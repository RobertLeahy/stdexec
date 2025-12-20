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
#include <stdexec/execution.hpp>

#include <type_traits>
#include <utility>

#include "../test_common/receivers.hpp"

using namespace exec;

namespace {

TEST_CASE("A non-void coroutine which co_returns immediately can be connected and started", "[coroutine_sender]") {
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

TEST_CASE("A void coroutine which co_returns immediately can be connected and started", "[coroutine_sender]") {
  bool invoked = false;
  const auto f = [&]() -> coroutine_sender<void> {
    invoked = true;
    co_return;
  };
  auto sender = f();
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  CHECK(!invoked);
  ::stdexec::start(op);
  CHECK(invoked);
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

TEST_CASE("A non-void coroutine which co_returns an rvalue reference can be connected and started", "[coroutine_sender]") {
  int i = 5;
  bool invoked = false;
  bool completed = false;
  const auto f = [&]() -> coroutine_sender<int&&> {
    invoked = true;
    co_return std::move(i);
  };
  auto sender = f();
  auto op = ::stdexec::connect(
    std::move(sender),
    make_fun_receiver([&](auto&& arg) noexcept {
      CHECK(!completed);
      completed = true;
      static_assert(std::is_same_v<int&&, decltype(arg)>);
      CHECK(arg == i);
      CHECK(&arg == &i);
    }));
  CHECK(!invoked);
  CHECK(!completed);
  ::stdexec::start(op);
  CHECK(invoked);
  CHECK(completed);
}

TEST_CASE("Abandoning the operation state without starting it cleans up the coroutine frame", "[coroutine_sender]") {
  const auto f = [&]() -> coroutine_sender<void> {
    co_return;
  };
  auto sender = f();
  auto op = ::stdexec::connect(
    std::move(sender),
    empty_recv::recv0{});
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

} // unnamed namespace
