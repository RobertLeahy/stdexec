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

#include <exec/observe.hpp>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>

#include "../test_common/receivers.hpp"
#include "../test_common/type_helpers.hpp"

namespace {

TEST_CASE("Noexcept observer correctly observes nullary value completion", "[observe]") {
  std::size_t invoked = 0;
  const auto observer = [&](::stdexec::set_value_t) noexcept {
    ++invoked;
  };
  static_assert(
    ::exec::detail::observe::is_observer_v<
      decltype(observer),
      ::stdexec::completion_signatures_of_t<
        decltype(::stdexec::just()),
        ::stdexec::env<>>>);
  auto sender = ::stdexec::just() | ::exec::observe(observer);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t()>,
      ::stdexec::completion_signatures_of_t<
        decltype(sender)&,
        ::stdexec::env<>>>);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t()>,
      ::stdexec::completion_signatures_of_t<
        decltype(sender)&&,
        ::stdexec::env<>>>);
  auto a = ::stdexec::connect(
    sender,
    expect_void_receiver{});
  auto b = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  CHECK(!invoked);
  ::stdexec::start(a);
  CHECK(invoked == 1);
  ::stdexec::start(b);
  CHECK(invoked == 2);
}

TEST_CASE("Possibly-throwing observer correctly observes nullary value completion when it does not happen to throw", "[observe]") {
  std::size_t invoked = 0;
  const auto observer = [&](::stdexec::set_value_t) {
    ++invoked;
  };
  auto sender = ::stdexec::just() | ::exec::observe(observer);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(),
        ::stdexec::set_error_t(std::exception_ptr)>,
      ::stdexec::completion_signatures_of_t<
        decltype(sender)&,
        ::stdexec::env<>>>);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(),
        ::stdexec::set_error_t(std::exception_ptr)>,
      ::stdexec::completion_signatures_of_t<
        decltype(sender)&&,
        ::stdexec::env<>>>);
  auto a = ::stdexec::connect(
    sender,
    expect_void_receiver{});
  auto b = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  CHECK(!invoked);
  ::stdexec::start(a);
  CHECK(invoked == 1);
  ::stdexec::start(b);
  CHECK(invoked == 2);
}

TEST_CASE("Possibly-throwing observer correctly observes nullary value completion and that completion is thereafter coalesced to an error when the observer throws", "[observe]") {
  std::size_t invoked = 0;
  const auto observer = [&](::stdexec::set_value_t) {
    ++invoked;
    throw std::logic_error("Test");
  };
  auto sender = ::exec::observe(::stdexec::just(), observer);
  auto a = ::stdexec::connect(
    sender,
    expect_error_receiver{});
  auto b = ::stdexec::connect(
    std::move(sender),
    expect_error_receiver{});
  CHECK(!invoked);
  ::stdexec::start(a);
  CHECK(invoked == 1);
  ::stdexec::start(b);
  CHECK(invoked == 2);
}

} // unnamed namespace
