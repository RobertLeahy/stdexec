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

#include <exec/enter.hpp>

#include <catch2/catch.hpp>
#include <exec/enter_sender.hpp>
#include <exec/exit_sender.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <exception>
#include <stdexcept>
#include <utility>

#include "../test_common/receivers.hpp"

namespace {

static_assert(
  ::exec::enter_sender_in<
    decltype(::exec::enter()),
    ::stdexec::env<>>);
static_assert(
  ::exec::enter_sender_in<
    decltype(::exec::enter(::stdexec::just(::stdexec::just()))),
    ::stdexec::env<>>);

TEST_CASE("Multiple scopes may be entered", "[construct]") {
  std::size_t undone = 0;
  auto undo = ::stdexec::just() | ::stdexec::then([&]() noexcept {
    ++undone;
  });
  auto succeed = ::stdexec::just(undo);
  auto sender = ::exec::enter(succeed, succeed);
  static_assert(
    ::exec::enter_sender_in<
      decltype(sender),
      ::stdexec::env<>>);
  auto op = ::stdexec::connect(
    std::move(sender),
    make_fun_receiver([&](auto&& sender) noexcept {
      static_assert(::exec::exit_sender<decltype(sender)>);
      CHECK(!undone);
      auto op = ::stdexec::connect(
        std::forward<decltype(sender)>(sender),
        expect_void_receiver{});
      CHECK(!undone);
      ::stdexec::start(op);
      CHECK(undone == 2);
    }));
  CHECK(!undone);
  ::stdexec::start(op);
  CHECK(undone == 2);
}

TEST_CASE("When an attempt is made to enter multiple scopes and entering one of them fails the effects of entering the other are undone", "[construct]") {
  bool undone = false;
  auto undo = ::stdexec::just() | ::stdexec::then([&]() noexcept {
    CHECK(!undone);
    undone = true;
  });
  auto succeed = ::stdexec::just(undo);
  auto fail = ::stdexec::just_error(
    std::make_exception_ptr(
      std::logic_error("TEST")));
  using enter_sender_type = ::exec::variant_sender<
    decltype(succeed),
    decltype(fail)>;
  static_assert(
    ::exec::enter_sender_in<
      enter_sender_type,
      ::stdexec::env<>>);
  auto sender = ::exec::enter(
    enter_sender_type(succeed),
    enter_sender_type(fail));
  static_assert(
    ::exec::enter_sender_in<
      decltype(sender),
      ::stdexec::env<>>);
  auto op = ::stdexec::connect(std::move(sender), expect_error_receiver{});
  CHECK(!undone);
  ::stdexec::start(op);
  CHECK(undone);
}

} // unnamed namespace
