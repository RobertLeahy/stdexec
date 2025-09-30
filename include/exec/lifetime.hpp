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
#include <cassert>
#include <cstddef>
#include <functional>
#include <tuple>
#include <type_traits>
#include <utility>

#include "inlinable_operation_state.hpp"
#include "like_t.hpp"
#include "object.hpp"
#include "storage_for_completion_signatures.hpp"
#include "storage_for_objects.hpp"
#include "variant_child_operation_state.hpp"

namespace exec {

namespace detail::lifetime {

template<typename>
struct tag {};

template<::exec::object... Objects>
using constructor_t = decltype(
  std::declval<::exec::storage_for_objects<Objects...>&>().construct(
    std::declval<Objects&>()...));

template<typename Function, ::exec::object... Objects>
using main_t = decltype(
  std::declval<::exec::storage_for_objects<Objects...>&>()(
    std::declval<Function>()));

template<::exec::object... Objects>
using destructor_t = decltype(
  std::declval<::exec::storage_for_objects<Objects...>&>().destroy(
    std::declval<Objects>()...));

template<::exec::object... Objects>
using stenciled_destructor_t = decltype(
  std::declval<::exec::storage_for_objects<Objects...>&>().destroy(
    std::declval<const std::array<bool, sizeof...(Objects)>&>(),
    std::declval<Objects>()...));

template<typename...>
using remove_completion_signature_t = ::stdexec::completion_signatures<>;

//  This finds the constructor completion signatures which will be passed
//  through unchanged (because they're sent immediately without being stored to
//  wait for destruction)
template<::stdexec::sender Constructor, typename Env>
  requires ::stdexec::sender_in<Constructor, Env>
using early_constructor_completion_signatures_t =
  ::stdexec::transform_completion_signatures<
    ::stdexec::completion_signatures_of_t<Constructor, Env>,
    ::stdexec::completion_signatures<>,
    remove_completion_signature_t>;

template<typename...>
struct unstencil_completion_signature;
//  This handles/removes set_value_t()
template<>
struct unstencil_completion_signature<> {
  using type = ::stdexec::completion_signatures<>;
};
template<std::size_t N, typename Tag, typename... Args>
  requires
    std::is_same_v<Tag, ::stdexec::set_stopped_t> ||
    std::is_same_v<Tag, ::stdexec::set_error_t>
struct unstencil_completion_signature<
  std::array<bool, N>,
  Tag,
  Args...>
{
  using type = ::stdexec::completion_signatures<Tag(Args...)>;
};

template<typename... Args>
using unstencil_completion_signature_t =
  typename unstencil_completion_signature<Args...>::type;

//  These are the constructor completion signatures which will be sent late,
//  i.e. which must be stored so one or more destructors can run after partial
//  construction
template<::stdexec::sender Constructor, typename Env>
  requires ::stdexec::sender_in<Constructor, Env>
using late_constructor_completion_signatures_t =
  ::stdexec::transform_completion_signatures<
    ::stdexec::completion_signatures_of_t<Constructor, Env>,
    ::stdexec::completion_signatures<>,
    unstencil_completion_signature_t,
    remove_completion_signature_t,
    ::stdexec::completion_signatures<>>;

template<::stdexec::sender Constructor, ::stdexec::sender Main, typename Env>
  requires
    ::stdexec::sender_in<Constructor, Env> &&
    ::stdexec::sender_in<Main, Env>
using stored_completion_signatures_t =
  ::stdexec::transform_completion_signatures<
    late_constructor_completion_signatures_t<Constructor, Env>,
    ::stdexec::completion_signatures_of_t<Main, Env>>;

template<::stdexec::sender Constructor, ::stdexec::sender Main, typename Env>
  requires
    ::stdexec::sender_in<Constructor, Env> &&
    ::stdexec::sender_in<Main, Env>
using storage_for_completion_signatures_t =
  ::exec::storage_for_completion_signatures<
    stored_completion_signatures_t<Constructor, Main, Env>>;

template<
  typename Function,
  ::stdexec::receiver Receiver,
  ::exec::object_in<::stdexec::env_of_t<Receiver>>... Objects>
struct operation_state
  : ::exec::inlinable_operation_state<
      operation_state<Function, Receiver, Objects...>,
      Receiver>,
    ::exec::storage_for_objects<Objects...>,
    ::exec::variant_child_operation_state<
      operation_state<Function, Receiver, Objects...>,
      tag,
      ::stdexec::env_of_t<Receiver>,
      constructor_t<Objects...>,
      main_t<Function, Objects...>,
      destructor_t<Objects...>,
      //  It's fine this is here in the case where sizeof...(Objects) == 1
      //  because the implementation of stenciled destroy coalesces to returning
      //  exactly the same thing as non-stenciled destroy in that case so we'll
      //  never increase the size of this object in that case
      stenciled_destructor_t<Objects...>>
{
private:
  using receiver_base_ = ::exec::inlinable_operation_state<
    operation_state,
    Receiver>;
  using storage_base_ = ::exec::storage_for_objects<Objects...>;
  using env_type_ = ::stdexec::env_of_t<Receiver>;
  using constructor_type_ = constructor_t<Objects...>;
  using main_type_ = main_t<Function, Objects...>;
  using completion_type_ = storage_for_completion_signatures_t<
    constructor_type_,
    main_type_,
    env_type_>;
  using destructor_type_ = destructor_t<Objects...>;
  using stenciled_destructor_type_ = stenciled_destructor_t<Objects...>;
  using children_base_ = ::exec::variant_child_operation_state<
    operation_state,
    tag,
    env_type_,
    constructor_type_,
    main_type_,
    destructor_type_,
    stenciled_destructor_type_>;
  Function f_;
  std::tuple<Objects...> objects_;
  //  Shame this is needed
  bool owns_constructor_{true};
  completion_type_ completion_;
  constexpr static bool construct_noexcept_ = noexcept(
    std::declval<children_base_&>().construct(
      std::declval<storage_base_&>().construct(
        std::declval<Objects&>()...)));
  constexpr void destroy_constructor_() noexcept {
    children_base_& base = *this;
    base.template destruct<constructor_type_>();
  }
  template<typename... Args>
  constexpr void main_complete_(Args&&... args) noexcept {
    completion_.arrive(std::forward<Args>(args)...);
    children_base_& base = *this;
    base.template destruct<main_type_>();
    std::apply(
      [&](Objects&&... objects) noexcept {
        storage_base_& storage = *this;
        base.template construct<destructor_type_>(
          storage.destroy(std::move(objects)...));
      },
      std::move(objects_));
    base.template start<destructor_type_>();
  }
  constexpr void stenciled_destroy_(
    const std::array<bool, sizeof...(Objects)>& stencil) noexcept
  {
    destroy_constructor_();
    children_base_& base = *this;
    std::apply(
      [&](Objects&&... objects) noexcept {
        storage_base_& storage = *this;
        base.template construct<stenciled_destructor_type_>(
          storage.destroy(
            stencil,
            std::move(objects)...));
      },
      std::move(objects_));
    base.template start<stenciled_destructor_type_>();
  }
  constexpr void finish_() noexcept {
    std::move(completion_).complete(std::move(this->get_receiver()));
  }
public:
  template<typename F, typename... Os>
    requires
      std::is_constructible_v<Function, F> &&
      (std::is_constructible_v<Objects, Os> && ...)
  constexpr explicit operation_state(
    F&& f,
    Receiver r,
    Os&&... os) noexcept(
      std::is_nothrow_constructible_v<Function, F> &&
      (std::is_nothrow_constructible_v<Objects, Os> && ...) &&
      construct_noexcept_)
      : receiver_base_(std::move(r)),
        f_(std::forward<F>(f)),
        objects_(std::forward<Os>(os)...)
  {
    std::apply(
      [&](Objects&... objects) noexcept(construct_noexcept_) {
        storage_base_& storage = *this;
        children_base_& children = *this;
        children.construct(storage.construct(objects...));
      },
      objects_);
  }
  constexpr ~operation_state() noexcept {
    if (owns_constructor_) {
      destroy_constructor_();
    }
  }
  void start() & noexcept {
    assert(owns_constructor_);
    owns_constructor_ = false;
    children_base_& base = *this;
    base.template start<constructor_type_>();
  }
  template<typename Tag>
  constexpr env_type_ get_env(const Tag&) noexcept {
    return ::stdexec::get_env(this->get_receiver());
  }
  //  Constructor completion signals
  constexpr void set_value(tag<constructor_type_>) noexcept {
    //  All objects constructed
    destroy_constructor_();
    storage_base_& storage = *this;
    constexpr auto noexcept_ = noexcept(
      std::declval<children_base_&>().construct(
        storage(std::move(f_))));
    children_base_& base = *this;
    const auto impl = [&]() noexcept(noexcept_) {
      base.construct(storage(std::move(f_)));
    };
    if constexpr (noexcept_) {
      impl();
    } else {
      try {
        impl();
      } catch (...) {
        ::stdexec::set_error(
          std::move(this->get_receiver()),
          std::current_exception());
        return;
      }
    }
    base.template start<main_type_>();
  }
  template<typename Tag, typename... Args>
  constexpr void set_value(
    tag<constructor_type_>,
    const std::array<bool, sizeof...(Objects)>& stencil,
    const Tag& tag,
    Args&&... args) noexcept
  {
    //  Objects indicated by the stencil are constructed and need to be
    //  destroyed before transmitting the stopped or error signal represented by
    //  the trailing arguments
    completion_.arrive(tag, std::forward<Args>(args)...);
    stenciled_destroy_(stencil);
  }
  template<typename... Args>
  constexpr void set_error(tag<constructor_type_>, Args&&... args) noexcept {
    //  No objects constructed, complete failure
    //
    //  Rather than destroying the constructor's operation state here we
    //  lifetime extend it in case the arguments we're passing through are
    //  references thereinto
    owns_constructor_ = true;
    ::stdexec::set_error(
      std::move(this->get_receiver()),
      std::forward<Args>(args)...);
  }
  template<typename... Args>
  constexpr void set_stopped(tag<constructor_type_>, Args&&... args) noexcept {
    //  No objects constructed, completely stopped
    //
    //  See rationale above for why we do this rather than destroying
    owns_constructor_ = true;
    ::stdexec::set_stopped(
      std::move(this->get_receiver()),
      std::forward<Args>(args)...);
  }
  //  Main completion signals
  template<typename... Args>
  constexpr void set_value(tag<main_type_>, Args&&... args) noexcept {
    main_complete_(::stdexec::set_value, std::forward<Args>(args)...);
  }
  template<typename... Args>
  constexpr void set_error(tag<main_type_>, Args&&... args) noexcept {
    main_complete_(::stdexec::set_error, std::forward<Args>(args)...);
  }
  template<typename... Args>
  constexpr void set_stopped(tag<main_type_>, Args&&... args) noexcept {
    main_complete_(::stdexec::set_stopped, std::forward<Args>(args)...);
  }
  //  Destructor completion signals
  template<typename T>
  constexpr void set_value(tag<T>) noexcept {
    children_base_& base = *this;
    base.template destruct<T>();
    finish_();
  }
};

template<typename Function, typename... Objects>
concept function =
  (::exec::object<Objects> && ...) &&
  requires(Function f, ::exec::storage_for_objects<Objects...> storage) {
    { storage(std::forward<Function>(f)) } -> ::stdexec::sender;
  };

template<typename Function, ::exec::object... Objects>
  requires function<Function, Objects...>
class sender {
  template<typename Receiver>
  using operation_state_ = operation_state<Function, Receiver, Objects...>;
  template<typename Self, typename Receiver>
  constexpr static bool noexcept_ = std::is_nothrow_constructible_v<
    operation_state_<Receiver>,
    ::exec::like_t<Self, Function>,
    Receiver,
    ::exec::like_t<Self, Objects>...>;
  using constructor_type_ = constructor_t<Objects...>;
public:
  Function f_;
  std::tuple<Objects...> objects_;
  using sender_concept = ::stdexec::sender_t;
  template<typename Self, typename Env>
    requires
      (::exec::object_in<Objects, Env> && ...) &&
      std::is_constructible_v<Function, ::exec::like_t<Self, Function>> &&
      (std::is_constructible_v<Objects, ::exec::like_t<Self, Objects>> && ...)
  consteval ::stdexec::transform_completion_signatures<
    early_constructor_completion_signatures_t<constructor_type_, Env>,
    typename storage_for_completion_signatures_t<
      constructor_type_,
      main_t<Function, Objects...>,
      Env>::completion_signatures> get_completion_signatures(
        this Self&&,
        const Env&) noexcept
  {
    return {};
  }
  template<typename Self, typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        Self,
        ::stdexec::env_of_t<Receiver>>>
  constexpr operation_state_<Receiver> connect(
    this Self&& self,
    Receiver r) noexcept(noexcept_<Self, Receiver>)
  {
    return std::apply(
      [&](auto&&... objects) noexcept(noexcept_<Self, Receiver>) {
        return operation_state_<Receiver>(
          std::forward<Self>(self).f_,
          std::move(r),
          std::forward<decltype(objects)>(objects)...);
      },
      std::forward<Self>(self).objects_);
  }
};

template<typename Function, typename... Objects>
using sender_for = sender<
  std::remove_cvref_t<Function>,
  std::remove_cvref_t<Objects>...>;

}

template<typename Function, typename... Objects>
  requires detail::lifetime::function<
    std::remove_cvref_t<Function>,
    std::remove_cvref_t<Objects>...>
constexpr detail::lifetime::sender<
  std::remove_cvref_t<Function>,
  std::remove_cvref_t<Objects>...> lifetime(
    Function&& f,
    Objects&&... os) noexcept(
      std::is_nothrow_constructible_v<
        std::remove_cvref_t<Function>,
        Function> &&
      (std::is_nothrow_constructible_v<
        std::remove_cvref_t<Objects>,
        Objects> && ...))
{
  return {
    std::forward<Function>(f),
    {std::forward<Objects>(os)...}};
}

}  // namespace exec
