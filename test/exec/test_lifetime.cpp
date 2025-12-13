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

#include <exec/lifetime.hpp>

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include <exec/construct.hpp>
#include <exec/object.hpp>
#include <exec/variant_sender.hpp>

#include "../test_common/receivers.hpp"
#include "../test_common/type_helpers.hpp"

namespace {

struct state {
  std::size_t& n;
  std::shared_ptr<int> construct_canary{std::make_shared<int>(5)};
  std::shared_ptr<int> destroy_canary{std::make_shared<int>(5)};
  std::optional<std::size_t> construct_invoked;
  std::optional<std::size_t> constructed;
  std::optional<std::size_t> destroy_invoked;
  std::optional<std::size_t> destroyed;
};

TEST_CASE("Single void async object works", "[lifetime]") {
  std::size_t n{0};
  state s{n};
  struct object {
    state& s_;
    auto construct() noexcept {
      CHECK(!s_.construct_invoked);
      s_.construct_invoked = ++s_.n;
      return
        ::stdexec::just(s_.construct_canary) |
        ::stdexec::then([&](const auto&) noexcept {
          CHECK(!s_.constructed);
          s_.constructed = ++s_.n;
        });
    };
    auto destroy() noexcept {
      CHECK(!s_.destroy_invoked);
      s_.destroy_invoked = ++s_.n;
      return
        ::stdexec::just(s_.destroy_canary) |
        ::stdexec::then([&](const auto&) noexcept {
          CHECK(!s_.destroyed);
          s_.destroyed = ++s_.n;
        });
    }
  };
  static_assert(::exec::object<object>);
  std::optional<std::size_t> invoked;
  std::optional<std::size_t> ran;
  const auto canary = std::make_shared<int>(5);
  auto f = [&]() noexcept {
    CHECK(!invoked);
    invoked = ++n;
    return
      ::stdexec::just(canary) |
      ::stdexec::then([&](const auto&) noexcept {
        CHECK(!ran);
        ran = ++n;
      });
  };
  auto sender = ::exec::lifetime(
    std::move(f),
    object{s});
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  CHECK(s.construct_canary.use_count() == 2);
  CHECK(s.destroy_canary.use_count() == 1);
  CHECK(s.construct_invoked == 1);
  CHECK(!s.constructed);
  CHECK(!s.destroy_invoked);
  CHECK(!s.destroyed);
  CHECK(canary.use_count() == 1);
  CHECK(!invoked);
  CHECK(!ran);
  ::stdexec::start(op);
  CHECK(s.construct_canary.use_count() == 1);
  CHECK(s.destroy_canary.use_count() == 1);
  CHECK(canary.use_count() == 1);
  CHECK(s.construct_invoked == 1);
  CHECK(s.constructed == 2);
  CHECK(s.destroy_invoked == 5);
  CHECK(s.destroyed == 6);
  CHECK(invoked == 3);
  CHECK(ran == 4);
}

struct int_object {
  state& s_;
  using type = int;
  type i_;
  auto construct(void* storage) noexcept {
    CHECK(!s_.construct_invoked);
    s_.construct_invoked = ++s_.n;
    return
      ::stdexec::just(s_.construct_canary) |
      ::stdexec::then([&, storage](const auto&) noexcept {
        CHECK(!s_.constructed);
        s_.constructed = ++s_.n;
        new(storage) int(i_);
      });
  };
  auto destroy(int* i) noexcept {
    CHECK(s_.construct_invoked);
    CHECK(s_.constructed);
    CHECK(!s_.destroy_invoked);
    CHECK(*i == i_);
    s_.destroy_invoked = ++s_.n;
    return
      ::stdexec::just(s_.destroy_canary) |
      ::stdexec::then([&, i](const auto&) noexcept {
        CHECK(!s_.destroyed);
        s_.destroyed = ++s_.n;
        CHECK(*i == i_);
        *i = 0;
      });
  }
};
static_assert(::exec::object<int_object>);

TEST_CASE("Single non-void async object works", "[lifetime]") {
  std::size_t n{0};
  state s{n};
  std::optional<std::size_t> invoked;
  std::optional<std::size_t> ran;
  const auto canary = std::make_shared<int>(5);
  auto f = [&](int& i) noexcept {
    CHECK(!invoked);
    CHECK(i == 5);
    invoked = ++n;
    return
      ::stdexec::just(canary) |
      ::stdexec::then([&](const auto&) noexcept {
        CHECK(!ran);
        ran = ++n;
        CHECK(i == 5);
      });
  };
  auto sender = ::exec::lifetime(
    std::move(f),
    int_object{s, 5});
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  CHECK(s.construct_canary.use_count() == 2);
  CHECK(s.destroy_canary.use_count() == 1);
  CHECK(s.construct_invoked == 1);
  CHECK(!s.constructed);
  CHECK(!s.destroy_invoked);
  CHECK(!s.destroyed);
  CHECK(canary.use_count() == 1);
  CHECK(!invoked);
  CHECK(!ran);
  ::stdexec::start(op);
  CHECK(s.construct_canary.use_count() == 1);
  CHECK(s.destroy_canary.use_count() == 1);
  CHECK(canary.use_count() == 1);
  CHECK(s.construct_invoked == 1);
  CHECK(s.constructed == 2);
  CHECK(s.destroy_invoked == 5);
  CHECK(s.destroyed == 6);
  CHECK(invoked == 3);
  CHECK(ran == 4);
}

TEST_CASE("Multiple non-void async objects work", "[lifetime]") {
  std::size_t n{0};
  state s1{n};
  state s2{n};
  std::optional<std::size_t> invoked;
  std::optional<std::size_t> ran;
  const auto canary = std::make_shared<int>(5);
  auto f = [&](int& i, int& j) noexcept {
    CHECK(!invoked);
    CHECK(i == 5);
    CHECK(j == 6);
    invoked = ++n;
    return
      ::stdexec::just(canary) |
      ::stdexec::then([&](const auto&) noexcept {
        CHECK(!ran);
        ran = ++n;
        CHECK(i == 5);
        CHECK(j == 6);
      });
  };
  auto sender = ::exec::lifetime(
    std::move(f),
    int_object{s1, 5},
    int_object{s2, 6});
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  CHECK(s1.construct_canary.use_count() == 2);
  CHECK(s1.destroy_canary.use_count() == 1);
  CHECK(s1.construct_invoked > 0);
  CHECK(s1.construct_invoked <= 2);
  CHECK(!s1.constructed);
  CHECK(!s1.destroy_invoked);
  CHECK(!s1.destroyed);
  CHECK(s2.construct_canary.use_count() == 2);
  CHECK(s2.destroy_canary.use_count() == 1);
  CHECK(s2.construct_invoked > 0);
  CHECK(s2.construct_invoked <= 2);
  CHECK(s1.construct_invoked != s2.construct_invoked);
  CHECK(!s2.constructed);
  CHECK(!s2.destroy_invoked);
  CHECK(!s2.destroyed);
  CHECK(canary.use_count() == 1);
  CHECK(!invoked);
  CHECK(!ran);
  ::stdexec::start(op);
  CHECK(s1.construct_canary.use_count() == 1);
  CHECK(s1.destroy_canary.use_count() == 1);
  CHECK(s2.construct_canary.use_count() == 1);
  CHECK(s2.destroy_canary.use_count() == 1);
  CHECK(canary.use_count() == 1);
  CHECK(s1.constructed > 2);
  CHECK(s1.constructed <= 4);
  CHECK(s2.constructed > 2);
  CHECK(s2.constructed <= 4);
  CHECK(s1.constructed != s2.constructed);
  CHECK(invoked == 5);
  CHECK(ran == 6);
  CHECK(s1.destroy_invoked > 6);
  CHECK(s1.destroy_invoked <= 8);
  CHECK(s2.destroy_invoked > 6);
  CHECK(s2.destroy_invoked <= 8);
  CHECK(s1.destroy_invoked != s2.destroy_invoked);
  CHECK(s1.destroyed > 8);
  CHECK(s1.destroyed <= 10);
  CHECK(s2.destroyed > 8);
  CHECK(s2.destroyed <= 10);
  CHECK(s1.destroyed != s2.destroyed);
}

TEST_CASE("Multiple non-void async objects work with a throwing invocable", "[lifetime]") {
  std::size_t n{0};
  state s1{n};
  state s2{n};
  std::optional<std::size_t> invoked;
  const auto canary = std::make_shared<int>(5);
  auto f = [&](int& i, int& j) -> decltype(::stdexec::just()) {
    CHECK(!invoked);
    CHECK(i == 5);
    CHECK(j == 6);
    invoked = ++n;
    throw std::logic_error("TESTING");
  };
  int_object o{s1, 5};
  static_assert(
    std::is_same_v<
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t()>,
      ::stdexec::completion_signatures_of_t<
        decltype(o.construct(nullptr)),
        ::stdexec::env<>>>);
  static_assert(
    std::is_same_v<
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t()>,
      ::stdexec::completion_signatures_of_t<
        decltype(
          ::exec::construct(
            o.construct(nullptr),
            o.construct(nullptr))),
        ::stdexec::env<>>>);
  auto sender = ::exec::lifetime(
    std::move(f),
    std::move(o),
    int_object{s2, 6});
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_error_receiver{});
  CHECK(s1.construct_canary.use_count() == 2);
  CHECK(s1.destroy_canary.use_count() == 1);
  CHECK(s1.construct_invoked > 0);
  CHECK(s1.construct_invoked <= 2);
  CHECK(!s1.constructed);
  CHECK(!s1.destroy_invoked);
  CHECK(!s1.destroyed);
  CHECK(s2.construct_canary.use_count() == 2);
  CHECK(s2.destroy_canary.use_count() == 1);
  CHECK(s2.construct_invoked > 0);
  CHECK(s2.construct_invoked <= 2);
  CHECK(s1.construct_invoked != s2.construct_invoked);
  CHECK(!s2.constructed);
  CHECK(!s2.destroy_invoked);
  CHECK(!s2.destroyed);
  CHECK(canary.use_count() == 1);
  CHECK(!invoked);
  ::stdexec::start(op);
  CHECK(s1.construct_canary.use_count() == 1);
  CHECK(s1.destroy_canary.use_count() == 1);
  CHECK(s2.construct_canary.use_count() == 1);
  CHECK(s2.destroy_canary.use_count() == 1);
  CHECK(canary.use_count() == 1);
  CHECK(s1.constructed > 2);
  CHECK(s1.constructed <= 4);
  CHECK(s2.constructed > 2);
  CHECK(s2.constructed <= 4);
  CHECK(s1.constructed != s2.constructed);
  CHECK(invoked == 5);
  CHECK(s1.destroy_invoked > 5);
  CHECK(s1.destroy_invoked <= 7);
  CHECK(s2.destroy_invoked > 5);
  CHECK(s2.destroy_invoked <= 7);
  CHECK(s1.destroy_invoked != s2.destroy_invoked);
  CHECK(s1.destroyed > 7);
  CHECK(s1.destroyed <= 9);
  CHECK(s2.destroyed > 7);
  CHECK(s2.destroyed <= 9);
  CHECK(s1.destroyed != s2.destroyed);
}

struct maybe_failing_int_object : int_object {
  bool fail_;
  auto construct(void* storage) noexcept {
    //  This has side effects so always call it
    auto ctor = int_object::construct(storage);
    auto fail =
      ::stdexec::just(s_.construct_canary) |
      ::stdexec::then([](const auto&) {
        throw std::logic_error("TEST");
      });
    using return_type = ::exec::variant_sender<
      decltype(ctor),
      decltype(fail)>;
    if (fail_) {
      return return_type(fail);
    }
    return return_type(ctor);
  }
};
static_assert(::exec::object<maybe_failing_int_object>);

TEST_CASE("When multiple async objects are constructed but construction of one of them fails the others are destroyed", "[lifetime]") {
  for (unsigned u = 0; u < 2; ++u) {
    std::size_t n{0};
    state s1{n};
    state s2{n};
    auto&& failing_state = u ? s1 : s2;
    auto&& succeeding_state = u ? s2 : s1;
    auto f = [&](auto&&...) noexcept {
      FAIL_CHECK("Unexpected invocation");
      return ::stdexec::just();
    };
    auto sender = ::exec::lifetime(
      std::move(f),
      maybe_failing_int_object{s1, 5, bool(u)},
      maybe_failing_int_object{s2, 6, !u});
    static_assert(
      set_equivalent<
        ::stdexec::completion_signatures_of_t<
          decltype(sender),
          ::stdexec::env<>>,
        ::stdexec::completion_signatures<
          ::stdexec::set_value_t(),
          ::stdexec::set_error_t(std::exception_ptr)>>);
    auto op = ::stdexec::connect(
      std::move(sender),
      expect_error_receiver{});
    CHECK(s1.construct_canary.use_count() == 2);
    CHECK(s1.destroy_canary.use_count() == 1);
    CHECK(s1.construct_invoked > 0);
    CHECK(s1.construct_invoked <= 2);
    CHECK(!s1.constructed);
    CHECK(!s1.destroy_invoked);
    CHECK(!s1.destroyed);
    CHECK(s2.construct_canary.use_count() == 2);
    CHECK(s2.destroy_canary.use_count() == 1);
    CHECK(s2.construct_invoked > 0);
    CHECK(s2.construct_invoked <= 2);
    CHECK(s1.construct_invoked != s2.construct_invoked);
    CHECK(!s2.constructed);
    CHECK(!s2.destroy_invoked);
    CHECK(!s2.destroyed);
    ::stdexec::start(op);
    CHECK(s1.construct_canary.use_count() == 1);
    CHECK(s1.destroy_canary.use_count() == 1);
    CHECK(s2.construct_canary.use_count() == 1);
    CHECK(s2.destroy_canary.use_count() == 1);
    CHECK(!failing_state.constructed);
    CHECK(succeeding_state.constructed == 3);
    CHECK(!failing_state.destroy_invoked);
    CHECK(succeeding_state.destroy_invoked == 4);
    CHECK(succeeding_state.destroyed == 5);
  }
}

} // unnamed namespace
