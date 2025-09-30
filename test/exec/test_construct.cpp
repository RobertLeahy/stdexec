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

#include <exec/construct.hpp>

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <functional>
#include <stdexcept>
#include <type_traits>

#include "../test_common/receivers.hpp"
#include "../test_common/type_helpers.hpp"

namespace {

TEST_CASE("When multiple constructors are run together and succeed a nullary value completion is sent", "[construct]") {
  std::size_t a = 0;
  std::size_t b = 0;
  auto sender = ::exec::construct(
    ::stdexec::just() | ::stdexec::then([&]() noexcept {
      ++a;
    }),
    ::stdexec::just() | ::stdexec::then([&]() noexcept {
      ++b;
    }));
  static_assert(
    !all_contained_in<
      ::stdexec::completion_signatures<
        ::stdexec::set_stopped_t()>,
      ::stdexec::completion_signatures_of_t<
        decltype(sender),
        ::stdexec::env<>>>);
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  CHECK(!a);
  CHECK(!b);
  ::stdexec::start(op);
  CHECK(a == 1);
  CHECK(b == 1);
}

TEST_CASE("A single constructor can be run", "[construct]") {
  std::size_t invoked = 0;
  auto sender = ::exec::construct(
    ::stdexec::just() | ::stdexec::then([&]() noexcept {
      ++invoked;
    }));
  static_assert(
    !all_contained_in<
      ::stdexec::completion_signatures<
        ::stdexec::set_stopped_t()>,
      ::stdexec::completion_signatures_of_t<
        decltype(sender),
        ::stdexec::env<>>>);
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  CHECK(!invoked);
  ::stdexec::start(op);
  CHECK(invoked == 1);
}

TEST_CASE("When one of several constructors could fail, but doesn't, success is reported", "[construct]") {
  std::size_t a = 0;
  std::size_t b = 0;
  auto sender = ::exec::construct(
    ::stdexec::just() | ::stdexec::then([&]() {
      ++a;
    }),
    ::stdexec::just() | ::stdexec::then([&]() noexcept {
      ++b;
    }));
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  CHECK(!a);
  CHECK(!b);
  ::stdexec::start(op);
  CHECK(a == 1);
  CHECK(b == 1);
}

TEST_CASE("When one of several constructors could fail, and does, partial success is reported", "[construct]") {
  std::size_t a = 0;
  std::size_t b = 0;
  auto sender =
    ::exec::construct(
      ::stdexec::just() | ::stdexec::then([&]() {
        ++a;
        throw std::logic_error("Test");
      }),
      ::stdexec::just() | ::stdexec::then([&]() noexcept {
        ++b;
      })) |
    ::stdexec::then([&](auto&&... args) noexcept {
      const auto f = [&](auto&& arr, ::stdexec::set_error_t, std::exception_ptr)
        noexcept
      {
        CHECK(!arr[0]);
        CHECK(arr[1]);
      };
      if constexpr (std::is_invocable_v<
        decltype(f)&,
        decltype(args)...>)
      {
        std::invoke(
          f,
          std::forward<decltype(args)>(args)...);
      } else {
        FAIL_CHECK("Unexpected parameters to set_value");
      }
    });
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_value_receiver{});
  CHECK(!a);
  CHECK(!b);
  ::stdexec::start(op);
  CHECK(a == 1);
  CHECK(b == 1);
}

TEST_CASE("When all of several constructors could fail, and do, complete failure is reported", "[construct]") {
  std::size_t a = 0;
  std::size_t b = 0;
  auto sender = ::exec::construct(
    ::stdexec::just() | ::stdexec::then([&]() {
      ++a;
      throw std::logic_error("A");
    }),
    ::stdexec::just() | ::stdexec::then([&]() {
      ++b;
      throw std::logic_error("B");
    }));
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_error_receiver{});
  CHECK(!a);
  CHECK(!b);
  ::stdexec::start(op);
  CHECK(a == 1);
  CHECK(b == 1);
}

} // unnamed namespace
