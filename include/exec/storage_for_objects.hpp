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

#include <array>
#include <cstddef>
#include <tuple>
#include <type_traits>
#include <utility>

#include "construct.hpp"
#include "object.hpp"
#include "storage_for_object.hpp"
#include "variant_sender.hpp"
#include "../stdexec/execution.hpp"

namespace exec {

namespace detail::storage_for_objects {

template<std::size_t N, ::exec::object Object>
struct storage_for_nth_object : ::exec::storage_for_object<Object> {};

template<typename, typename>
struct invoke_result;
template<typename Function, typename... Args>
struct invoke_result<Function, std::tuple<Args...>> {
  using type = std::invoke_result_t<
    Function,
    Args...>;
};

template<typename, typename>
struct is_nothrow_invocable;
template<typename Function, typename... Args>
struct is_nothrow_invocable<Function, std::tuple<Args...>> {
  constexpr static bool value = std::is_nothrow_invocable_v<
    Function,
    Args...>;
};

template<typename, object...>
struct base;

template<std::size_t... Ns, object... Objects>
struct base<
  std::index_sequence<Ns...>,
  Objects...>
  : storage_for_nth_object<Ns, Objects>...
{
  constexpr ::stdexec::sender auto construct(Objects&... objects) noexcept(
    noexcept(
      ::exec::construct(
        std::declval<::exec::storage_for_object<Objects>&>().construct(
          objects)...)))
  {
    return ::exec::construct(
      static_cast<::exec::storage_for_object<Objects>&>(
        static_cast<storage_for_nth_object<Ns, Objects>&>(*this))
          .construct(objects)...);
  }
  template<std::size_t N>
  constexpr decltype(auto) get() noexcept {
    using type = std::tuple_element_t<
      N,
      std::tuple<Objects...>>;
    return static_cast<::exec::storage_for_object<type>&>(
      static_cast<storage_for_nth_object<N, type>&>(*this));
  }
  using arguments_type = decltype(
    std::tuple_cat(
      std::declval<storage_for_nth_object<Ns, Objects>&>().get_argument()...));
  constexpr arguments_type get_arguments() noexcept {
    return std::tuple_cat(
      static_cast<storage_for_nth_object<Ns, Objects>&>(*this)
        .get_argument()...);
  }
  template<typename Function>
  constexpr auto operator()(Function&& f) noexcept(
    is_nothrow_invocable<Function, arguments_type>::value)
    -> typename invoke_result<Function, arguments_type>::type
  {
    return std::apply(
      std::forward<Function>(f),
      get_arguments());
  }
  constexpr ::stdexec::sender auto destroy(Objects&&... objects) noexcept {
    return ::stdexec::when_all(
      static_cast<storage_for_nth_object<Ns, Objects>&>(*this).destroy(
        std::move(objects))...);
  }
  constexpr ::stdexec::sender auto destroy(
    const std::array<bool, sizeof...(Objects)>& stencil,
    Objects&&... objects) noexcept
  {
    //  This branch means we don't need to metaprogram around calling this
    //  function
    if constexpr (sizeof...(Objects) == 1) {
      (void)stencil;
      return destroy(std::move(objects)...);
    } else {
      const auto impl = [&]<std::size_t N>(
        std::tuple_element_t<
          N,
          std::tuple<Objects...>>&& object,
        std::integral_constant<std::size_t, N>) noexcept
      {
        auto fallback = ::stdexec::just();
        storage_for_nth_object<
          N,
          std::tuple_element_t<
            N,
            std::tuple<Objects...>>>& base = *this;
        using destructor_type = decltype(base.destroy(std::move(object)));
        using return_type = ::exec::variant_sender<
          decltype(fallback),
          destructor_type>;
        if (stencil[N]) {
          return return_type(base.destroy(std::move(object)));
        }
        return return_type(std::move(fallback));
      };
      return ::stdexec::when_all(
        impl(
          std::move(objects),
          std::integral_constant<std::size_t, Ns>{})...);
    }
  }
};

}

template<object... Objects>
struct storage_for_objects
  : detail::storage_for_objects::base<
      std::index_sequence_for<Objects...>,
      Objects...>
{};

}  // namespace exec
