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

#include <chrono>
//#include <concepts>
//#include <cstddef>
#include <exception>
#include <functional>
//#include <memory>
//#include <mutex>
//#include <optional>
//#include <stdexcept>
//#include <thread>
#include <type_traits>
#include <utility>
#include <variant>
#include <asioexec/asio_config.hpp>
#include <asioexec/use_sender.hpp>
#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>
#include <test_common/receivers.hpp>
#include <test_common/type_helpers.hpp>

using namespace stdexec;
using namespace asioexec;

namespace {

  static_assert(set_equivalent<
                ::stdexec::completion_signatures<
                  ::stdexec::set_value_t(int&, float&),
                  ::stdexec::set_value_t(const int&, const float&),
                  ::stdexec::set_value_t(int, float),
                  ::stdexec::set_error_t(int&),
                  ::stdexec::set_error_t(const int&),
                  ::stdexec::set_error_t(int),
                  ::stdexec::set_stopped_t()>,
                detail::let_io_context::completion_signatures<::stdexec::completion_signatures<
                  ::stdexec::set_value_t(int&, float&),
                  ::stdexec::set_value_t(const int&, const float&),
                  ::stdexec::set_value_t(int&&, float&&),
                  ::stdexec::set_value_t(int, float),
                  ::stdexec::set_error_t(int&),
                  ::stdexec::set_error_t(const int&),
                  ::stdexec::set_error_t(int),
                  ::stdexec::set_error_t(int&&),
                  ::stdexec::set_stopped_t()>>>);

  static_assert(std::is_same_v<
                detail::let_io_context::tuple<::stdexec::set_value_t(int&, int&&)>::type,
                std::tuple<::stdexec::set_value_t, int&, int>>);

  TEST_CASE(
    "Tests the implementation detail that stores completion signals",
    "[asioexec][let_io_context]") {
    {
      const detail::let_io_context::storage<::stdexec::completion_signatures<>> storage;
      (void) storage;
    }
    using variant = std::variant<std::monostate, int, std::reference_wrapper<int>>;

    struct receiver : public base_expect_receiver<> {
      variant& v_;

      void set_value(int&& i) && noexcept {
        set_called();
        v_.emplace<int>(i);
      }

      void set_value(int& i) && noexcept {
        set_called();
        v_.emplace<std::reference_wrapper<int>>(i);
      }
    };

    {
      variant v;
      detail::let_io_context::storage<
        ::stdexec::completion_signatures<::stdexec::set_value_t(int), ::stdexec::set_value_t(int&)>>
        storage;
      storage.arrive(::stdexec::set_value, 5);
      std::move(storage).complete(receiver{{}, v});
      CHECK(std::get<int>(v) == 5);
    }
    {
      int i = 5;
      variant v;
      detail::let_io_context::storage<
        ::stdexec::completion_signatures<::stdexec::set_value_t(int), ::stdexec::set_value_t(int&)>>
        storage;
      storage.arrive(::stdexec::set_value, i);
      std::move(storage).complete(receiver{{}, v});
      CHECK(&std::get<std::reference_wrapper<int>>(v).get() == &i);
    }
  }

  TEST_CASE(
    "A simple asynchronous operation is run against a provided io_context",
    "[asioexec][let_io_context]") {
    auto sender = let_io_context([](auto&& ctx) {
      return ::stdexec::just(asio_impl::system_timer(ctx)) | ::stdexec::let_value([](auto&& timer) {
               timer.expires_after(std::chrono::milliseconds(1));
               return timer.async_wait(use_sender);
             });
    });
    static_assert(set_equivalent<
                  ::stdexec::completion_signatures<
                    ::stdexec::set_value_t(),
                    ::stdexec::set_error_t(std::exception_ptr),
                    ::stdexec::set_stopped_t()>,
                  ::stdexec::completion_signatures_of_t<decltype(sender), ::stdexec::env<>>>);
    static_assert(set_equivalent<
                  ::stdexec::completion_signatures<
                    ::stdexec::set_value_t(),
                    ::stdexec::set_error_t(std::exception_ptr),
                    ::stdexec::set_stopped_t()>,
                  ::stdexec::completion_signatures_of_t<const decltype(sender)&, ::stdexec::env<>>>);
    {
      auto op = ::stdexec::connect(sender, expect_void_receiver{});
      ::stdexec::start(op);
    }
    {
      auto op = ::stdexec::connect(std::move(sender), expect_void_receiver{});
      ::stdexec::start(op);
    }
  }

} // namespace
