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

#include <tuple>
#include <type_traits>
#include <variant>
#include <asioexec/asio_config.hpp>
#include <stdexec/execution.hpp>

namespace asioexec {

  namespace detail::let_io_context {

    template<typename>
    struct decay;
    template<typename T>
      requires (
        !std::is_lvalue_reference_v<T> &&
        std::is_constructible_v<std::decay_t<T>, T>)
    struct decay<T> : std::decay<T> {};
    template<typename T>
    struct decay<T&> {
      using type = T&;
    };

    template<typename... Args>
    using transform_set_value = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(typename decay<Args>::type...)>;
    template<typename T>
    using transform_set_error = ::stdexec::completion_signatures<
      ::stdexec::set_error_t(typename decay<T>::type)>;

    template<typename Signatures>
    using completion_signatures = ::stdexec::transform_completion_signatures<
      Signatures,
      ::stdexec::completion_signatures<>,
      transform_set_value,
      transform_set_error>;

    template<typename>
    struct tuple;
    template<typename Tag, typename... Args>
    struct tuple<Tag(Args...)> {
      using type = std::tuple<
        Tag,
        typename decay<Args>::type...>;
    };

    template<typename>
    class storage;
    template<>
    class storage<::stdexec::completion_signatures<>> {
    public:
      template<::stdexec::receiver Receiver>
      static constexpr void complete(const Receiver&) noexcept {
        STDEXEC_UNREACHABLE();
      }
    };
    template<typename... Signatures>
    class storage<::stdexec::completion_signatures<Signatures...>> {
      using storage_type_ = std::variant<
        std::monostate,
        typename tuple<Signatures>::type...>;
      storage_type_ storage_;
      template<typename Tag, typename... Args>
      static constexpr bool noexcept_ = std::is_nothrow_constructible_v<
        typename tuple<Tag(Args...)>::type,
        Tag,
        Args...>;
      template<typename Receiver, typename... Args>
      constexpr void complete_(Receiver&& r, std::tuple<Args...>&& t) noexcept {
        std::apply(
          [&](const auto& tag, auto&&... args) noexcept {
            tag(
              static_cast<Receiver&&>(r),
              static_cast<decltype(args)&&>(args)...);
          },
          static_cast<std::tuple<Args...>&&>(t));
      }
      template<typename Receiver>
      constexpr void complete_(const Receiver&, const std::monostate&) noexcept
      {
        STDEXEC_UNREACHABLE();
      }
    public:
      template<typename... Args>
      constexpr void arrive(Args&&... args) noexcept(noexcept_<Args...>) {
        STDEXEC_ASSERT(std::holds_alternative<std::monostate>(storage_));
        const auto impl = [&]() noexcept(noexcept_<Args...>) {
          storage_.template emplace(static_cast<Args&&>(args)...);
        };
        if constexpr (noexcept(impl())) {
          impl();
        } else {
          try {
            impl();
          } catch (...) {
            storage_ = std::monostate{};
            throw;
          }
        }
      }
      template<typename Receiver>
      constexpr void complete(Receiver&& r) && noexcept {
        std::visit(
          [&](auto&& alternative) noexcept {
            complete_(static_cast<Receiver&&>(r), static_cast<decltype(alternative)&&>(alternative));
          },
          static_cast<storage_type_&&>(storage_));
      }
    };

  }

}
