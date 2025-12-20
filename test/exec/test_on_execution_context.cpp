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

#include <exec/on_execution_context.hpp>

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>

//#include <cstddef>
//#include <exception>
//#include <optional>
//#include <stdexcept>
//#include <string_view>
//#include <system_error>
#include <utility>

#include "../test_common/receivers.hpp"
#include "../test_common/type_helpers.hpp"

using namespace exec;

namespace {

TEST_CASE("Senders can be run on an execution context even if they don't use that execution context", "[on_execution_context]") {
  auto sender = ::stdexec::just() | on_execution_context<::stdexec::run_loop>();
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures_of_t<
        decltype(sender),
        ::stdexec::env<>>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t()>>);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures_of_t<
        const decltype(sender)&,
        ::stdexec::env<>>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t()>>);
  auto op = ::stdexec::connect(std::move(sender), expect_void_receiver{});
  ::stdexec::start(op);
}

TEST_CASE("Senders can be run on an execution context and make use of that execution context", "[on_execution_context]") {
  auto sender =
    ::stdexec::read_env(::stdexec::get_scheduler) |
    ::stdexec::let_value([](const auto scheduler) {
      return ::stdexec::starts_on(scheduler, ::stdexec::just());
    }) |
    on_execution_context<::stdexec::run_loop>();
  auto op = ::stdexec::connect(std::move(sender), expect_void_receiver{});
  ::stdexec::start(op);
}

} // unnamed namespace
