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

#include "../stdexec/execution.hpp"

#include <algorithm>
#include <concepts>
#include <cstddef>
#include <memory>
#include <new>
#include <utility>

namespace exec {

struct get_uninitialized_storage_t {
  template<typename Env>
    requires requires (const Env e, const get_uninitialized_storage_t& self) {
      { e.query(self) } noexcept -> std::same_as<void*>;
    }
  constexpr void* operator()(const Env& e) const noexcept {
    return e.query(*this);
  }
};

constexpr inline get_uninitialized_storage_t get_uninitialized_storage;

namespace detail::with {

template<typename T>
concept object = std::is_object_v<T>;

template<typename T>
concept object_reference =
  std::is_lvalue_reference_v<T> &&
  object<std::remove_reference_t<T>>;

template<typename T>
concept object_pointer =
  std::is_pointer_v<T> &&
  object<std::remove_pointer_t<T>>;

}

namespace detail::with {

template<typename>
class storage;

template<typename T>
  requires object<T>
class storage<T> {
  alignas(T) std::byte storage_[sizeof(T)];
public:
  //  For the constructor
  constexpr void* get_uninitialized_storage() noexcept {
    return storage_;
  }
  //  For the destructor
  constexpr T* get_initialized_storage() noexcept {
    return std::launder(
      static_cast<T*>(
        get_uninitialized_storage()));
  }
  //  For the operation actually using the stored object
  constexpr T& get_object() noexcept {
    return *get_initialized_storage();
  }
  //  TODO
  template<typename Env>
  constexpr Env get_construct_env(Env e) noexcept(
    std::is_nothrow_move_constructible_v<Env>)
  {
    return e;
  }
  //  TODO
  template<typename Env>
  constexpr Env get_destroy_env(Env e) noexcept(
    std::is_nothrow_move_constructible_v<Env>)
  {
    return e;
  }
};

template<>
struct storage<void> {
  template<typename Env>
  constexpr Env get_construct_env(Env e) noexcept(
    std::is_nothrow_move_constructible_v<Env>)
  {
    return e;
  }
  template<typename Env>
  constexpr Env get_destroy_env(Env e) noexcept(
    std::is_nothrow_move_constructible_v<Env>)
  {
    return e;
  }
};

template<std::size_t I>
struct constructed_tag : std::integral_constant<std::size_t, I> {};
template<std::size_t I>
struct destroyed_tag : std::integral_constant<std::size_t, I> {};

template<typename T>
constexpr void decay(T&&) noexcept(
  std::is_nothrow_constructible_v<std::decay_t<T>, T>);

template<typename Object, typename T>
  requires requires(Object& object, storage<T>& storage) {
    { object.construct(storage.get_uninitialized_storage()) } -> ::stdexec::sender;
  }
constexpr auto get_constructor(Object& object, storage<T>& storage)
  noexcept(noexcept(with::decay(object.construct(storage.get_uninitialized_storage()))))
{
  return object.construct(storage.get_uninitialized_storage());
}

template<typename Object, typename T>
  requires requires(Object& object) {
    { object.construct() } -> ::stdexec::sender;
  }
constexpr auto get_constructor(Object& object, storage<T>&) noexcept(
  noexcept(with::decay(object.construct())))
{
  return object.construct();
}

template<typename Object, typename T>
  requires requires(Object& object, storage<T>& storage) {
    { object.destroy(storage.get_initialized_storage()) } -> ::stdexec::sender;
  }
constexpr auto get_destructor(Object& object, storage<T>& storage)
  noexcept(noexcept(with::decay(object.destroy(storage.get_initialized_storage()))))
{
  return object.destroy(storage.get_initialized_storage());
}

template<typename Object, typename T>
  requires requires(Object& object) {
    { object.destroy() } -> ::stdexec::sender;
  }
constexpr auto get_destructor(Object& object, storage<T>&) noexcept(
  noexcept(with::decay(object.destroy())))
{
  return object.destroy();
}

template<
  typename Derived,
  typename Env,
  std::size_t I,
  typename Object>
class object_state {
private:
  static_assert(std::is_nothrow_move_constructible_v<Env>);
  using type_ = typename Object::type;
  using storage_type_ = storage<type_>;
  using constructed_tag_ = constructed_tag<I>;
  using destroyed_tag_ = destroyed_tag<I>;
  using construct_env_ = decltype(
    std::declval<storage_type_&>().get_construct_env(std::declval<Env>()));
  using destroy_env_ = decltype(
    std::declval<storage_type_&>().get_destroy_env(std::declval<Env>()));
  constexpr Derived& get_derived_() noexcept {
    return static_cast<Derived&>(*this);
  }
  constexpr Env get_env_() noexcept {
    static_assert(noexcept(Env(get_derived_().get_env())));
    return Env(get_derived_().get_env());
  }
  struct construct_receiver_ {
    using receiver_concept = ::stdexec::receiver_t;
    constexpr explicit construct_receiver_(object_state& self) noexcept
      : self_(self)
    {}
    constexpr void set_value() && noexcept {
      //  TODO
    }
    template<typename... Args>
    constexpr void set_error(Args&&... args) && noexcept {
      //  TODO
    }
    template<typename... Args>
    constexpr void set_stopped(Args&&... args) && noexcept {
      //  TODO
    }
    constexpr construct_env_ get_env() noexcept {
      return self_.object_storage_.get_construct_env(self_.get_env_());
    }
  private:
    object_state& self_;
  };
  using constructor_ = decltype(
    with::get_constructor(
      std::declval<Object&>(),
      std::declval<storage_type_&>()));
  using construct_operation_state_ = ::stdexec::connect_result_t<
    constructor_,
    construct_receiver_>;
  struct destroy_receiver_ {
    using receiver_concept = ::stdexec::receiver_t;
    constexpr explicit destroy_receiver_(object_state& self) noexcept
      : self_(self)
    {}
    constexpr void set_value() && noexcept {
      //  TODO
    }
    constexpr destroy_env_ get_env() noexcept {
      return self_.object_storage_.get_destroy_env(self_.get_env_());
    }
  private:
    object_state& self_;
  };
  using destructor_ = decltype(
    with::get_destructor(
      std::declval<Object&>(),
      std::declval<storage_type_&>()));
  using destroy_operation_state_ = ::stdexec::connect_result_t<
    destructor_,
    destroy_receiver_>;
  static_assert(
    noexcept(
      ::stdexec::connect(
        std::declval<destructor_>(),
        std::declval<destroy_receiver_>())));
  static constexpr auto align_ = std::max(
    alignof(construct_operation_state_),
    alignof(destroy_operation_state_));
  static constexpr auto size_ = std::max(
    sizeof(construct_operation_state_),
    sizeof(destroy_operation_state_));
  alignas(align_) std::byte operation_state_storage_[size_];
  storage_type_ object_storage_;
  template<typename OperationState, typename Receiver, typename Sender>
  constexpr decltype(auto) connect_(Sender&& sender) noexcept(
    noexcept(
      ::stdexec::connect(
        std::declval<Sender>(),
        std::declval<Receiver>())))
  {
    return *new(operation_state_storage_) OperationState(
      ::stdexec::connect(
        (Sender&&)sender,
        Receiver(*this)));
  }
public:
  object_state(const object_state&) = delete;
  object_state& operator=(const object_state&) = delete;
  //constexpr object_state(
  //  constructor_&& constructor,
  //  destructor_&& destructor) noexcept(
  //    std::is_nothrow_move_constructible_v<destructor_> &&
  //    noexcept(
  //      ::stdexec::connect(
  //        std::move(constructor),
  //        std::declval<construct_receiver_>())))
  //{
  //  connect_<construct_operation_state_, construct_receiver_>(
  //    (Constructor&&)constructor);
  //}
  constexpr ~object_state() noexcept {
    //  TODO
  }
  //  TODO
  void destroy() noexcept {
    //  TODO
  }
};


}

struct with_t {
  //  TODO
};

constexpr inline with_t with;

} // namespace exec
