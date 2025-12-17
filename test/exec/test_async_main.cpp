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

#include <algorithm>
#include <chrono>
#include <exception>
#include <span>
#include <stdexcept>
#include <string_view>
#include <thread>
#include <utility>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

template <typename Receiver>
struct thread_receiver {
  using receiver_concept = ::stdexec::receiver_t;
  Receiver& r_;
  constexpr decltype(auto) get_env() const noexcept {
    return ::stdexec::get_env(r_);
  }
  template <typename... Args>
  constexpr void set_value(Args&&... args) && noexcept {
    ::stdexec::set_value(std::move(r_), std::forward<Args>(args)...);
  }
  template <typename... Args>
  constexpr void set_error(Args&&... args) && noexcept {
    ::stdexec::set_error(std::move(r_), std::forward<Args>(args)...);
  }
  template <typename... Args>
  constexpr void set_stopped(Args&&... args) && noexcept {
    ::stdexec::set_stopped(std::move(r_), std::forward<Args>(args)...);
  }
};

template <typename Sender, typename Receiver>
struct thread_op_state {
  Receiver r_;
  ::stdexec::connect_result_t<Sender, thread_receiver<Receiver>> op_;
  constexpr thread_op_state(Sender&& s, Receiver r)
    : r_(std::move(r))
    , op_(::stdexec::connect(std::forward<Sender>(s), thread_receiver{r_})) {
  }
  void start() & noexcept {
    try {
      std::thread([&]() noexcept { ::stdexec::start(op_); }).detach();
    } catch (...) {
      ::stdexec::set_error(std::move(r_), std::current_exception());
    }
  }
};

template <::stdexec::sender Sender>
struct thread_sender {
  using sender_concept = ::stdexec::sender_t;
  Sender s_;
  template <typename Env>
  consteval ::stdexec::transform_completion_signatures<
    ::stdexec::completion_signatures_of_t<const Sender&, Env>,
    ::stdexec::completion_signatures<::stdexec::set_error_t(std::exception_ptr)>>
    get_completion_signatures(const Env&) const noexcept {
    return {};
  }
  template <typename Receiver>
    requires ::stdexec::sender_to<const Sender&, Receiver>
  constexpr auto connect(Receiver r) {
    return thread_op_state<const Sender&, Receiver>(s_, std::move(r));
  }
};

auto async_main(std::span<const char* const> argv) {
  auto success = ::stdexec::just();
  auto fail = ::stdexec::just_error(5);
  auto exception = ::stdexec::just()
                 | ::stdexec::then([]() { throw std::runtime_error("Throwing as requested"); });
  auto wait = ::stdexec::just() | ::stdexec::then([]() {
                std::this_thread::sleep_for(std::chrono::milliseconds(250));
              });
  auto thread_success = thread_sender{wait};
  auto thread_fail = thread_sender{
    wait | ::stdexec::let_value([]() { return ::stdexec::just_error(5); })};
  auto thread_exception = thread_sender{
    wait | ::stdexec::then([]() { throw std::runtime_error("Throwing as requested"); })};
  using type = ::exec::variant_sender<
    decltype(success),
    decltype(fail),
    decltype(exception),
    decltype(thread_success),
    decltype(thread_fail),
    decltype(thread_exception)>;
  if (argv.size() < 2) {
    return type(std::move(success));
  }
  argv = argv.subspan(1);
  const auto check = [&](const std::string_view sv) noexcept {
    return std::find(argv.begin(), argv.end(), sv) != argv.end();
  };
  const auto should_wait = check("wait");
  if (check("fail")) {
    if (should_wait) {
      return type(thread_fail);
    }
    return type(fail);
  }
  if (check("throw")) {
    if (should_wait) {
      return type(thread_exception);
    }
    return type(exception);
  }
  if (should_wait) {
    return type(thread_success);
  }
  return type(success);
}

#include <exec/async_main.hpp>
