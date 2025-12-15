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

#pragma once

#if __has_include(<pthread.h>)
#  include <pthread.h>
#elif __has_include(<windows.h>)
#  include <windows.h>
#else
#  error "Pthreads or WinAPI required"
#endif

#include <concepts>
#include <cstdlib>
#include <exception>
#include <optional>
#include <type_traits>
#include <utility>
#include <stdexec/execution.hpp>

namespace exec {

  namespace detail::main {

    template <typename Sender>
    struct receiver {
      using receiver_concept = ::stdexec::receiver_t;
      [[noreturn]]
      void set_value() && noexcept {
        destroy_();
        std::exit(EXIT_SUCCESS);
      }
      [[noreturn]]
      void set_error(const int code) && noexcept {
        destroy_();
        STDEXEC_ASSERT(code != EXIT_SUCCESS);
        std::exit(code);
      }
      [[noreturn]]
      void set_error(std::exception_ptr ex) && noexcept {
        destroy_();
        STDEXEC_ASSERT(ex);
        try {
          std::rethrow_exception(std::move(ex));
        } catch (...) {
          std::terminate();
        }
      }
     private:
      static void destroy_() noexcept;
    };

    template <typename Sender>
    concept sender = ::stdexec::sender_to<Sender, receiver<Sender>>;

    template <typename Sender>
    inline std::optional<::stdexec::connect_result_t<Sender, receiver<Sender>>> op;

    template <typename Sender>
    inline void receiver<Sender>::destroy_() noexcept {
      op<Sender>.reset();
    }

  } // namespace detail::main

  template <std::invocable Factory>
    requires detail::main::sender<std::invoke_result_t<Factory>>
  [[noreturn]]
  void main(Factory f) {
    using sender_type = decltype(std::move(f)());
    ::stdexec::start(detail::main::op<sender_type>.emplace(::stdexec::__emplace_from([&]() {
      return ::stdexec::connect(std::move(f)(), detail::main::receiver<sender_type>{});
    })));
#if __has_include(<pthread.h>)
    ::pthread_exit(nullptr);
#else
    ::ExitThread(0);
#endif
  }

} // namespace exec
