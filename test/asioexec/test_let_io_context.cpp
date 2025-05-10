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

#include <asioexec/let_io_context.hpp>

//#include <chrono>
//#include <concepts>
//#include <cstddef>
//#include <exception>
//#include <functional>
//#include <memory>
//#include <mutex>
//#include <optional>
//#include <stdexcept>
//#include <thread>
#include <type_traits>
//#include <utility>
//#include <asioexec/asio_config.hpp>
#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>
//#include <test_common/receivers.hpp>
#include <test_common/type_helpers.hpp>

using namespace stdexec;
using namespace asioexec;

namespace {

  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(int&, float&),
        ::stdexec::set_value_t(const int&, const float&),
        ::stdexec::set_value_t(int, float),
        ::stdexec::set_error_t(int&),
        ::stdexec::set_error_t(const int&),
        ::stdexec::set_error_t(int),
        ::stdexec::set_stopped_t()>,
      detail::let_io_context::completion_signatures<
        ::stdexec::completion_signatures<
          ::stdexec::set_value_t(int&, float&),
          ::stdexec::set_value_t(const int&, const float&),
          ::stdexec::set_value_t(int&&, float&&),
          ::stdexec::set_value_t(int, float),
          ::stdexec::set_error_t(int&),
          ::stdexec::set_error_t(const int&),
          ::stdexec::set_error_t(int),
          ::stdexec::set_error_t(int&&),
          ::stdexec::set_stopped_t()>>>);

  static_assert(
    std::is_same_v<
      detail::let_io_context::tuple<
        ::stdexec::set_value_t(int&, int&&)>::type,
      std::tuple<
        ::stdexec::set_value_t,
        int&,
        int>>);

  TEST_CASE(
    "Tests the implementation detail that stores completion signals", "[asioexec][completion_token]")
  {
    {
      const detail::let_io_context::storage<
        ::stdexec::completion_signatures<>> storage;
      (void)storage;
    }
    {
      detail::let_io_context::storage<
        ::stdexec::completion_signatures<
          ::stdexec::set_value_t(int),
          ::stdexec::set_value_t(int&)>> storage;
      storage.arrive(::stdexec::set_value, 5);
    }
  }

  //TEST_CASE(
  //  "When the operation declares separate rvalue and const lvalue completion signatures they are "
  //  "appropriately passed through even if the lvalue is sent mutable",
  //  "[asioexec][completion_token]") {
  //  const auto initiating_function = [](const bool rvalue, auto&& token) {
  //    return asio_impl::async_initiate<decltype(token), void(std::mutex &&), void(const std::mutex&)>(
  //      [rvalue](auto&& h) {
  //        std::mutex m;
  //        if (rvalue) {
  //          std::invoke(std::forward<decltype(h)>(h), std::move(m));
  //        } else {
  //          std::invoke(std::forward<decltype(h)>(h), m);
  //        }
  //      },
  //      token);
  //  };
  //  value_category_receiver::kind rvalue_kind{value_category_receiver::kind::none};
  //  value_category_receiver::kind lvalue_kind{value_category_receiver::kind::none};
  //  auto rvalue = connect_shared(
  //    initiating_function(true, completion_token), value_category_receiver(rvalue_kind));
  //  auto lvalue = connect_shared(
  //    initiating_function(false, completion_token), value_category_receiver(lvalue_kind));
  //  CHECK(rvalue_kind == value_category_receiver::kind::none);
  //  start_shared(std::move(rvalue));
  //  CHECK(rvalue_kind == value_category_receiver::kind::rvalue);
  //  CHECK(lvalue_kind == value_category_receiver::kind::none);
  //  start_shared(std::move(lvalue));
  //  CHECK(lvalue_kind == value_category_receiver::kind::const_lvalue);
  //}

} // namespace
