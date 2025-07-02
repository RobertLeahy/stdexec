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
#include <array>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <exception>
#include <memory>
#include <new>
#include <optional>
#include <tuple>
#include <utility>
#include "child_operation_state.hpp"
#include "inlinable_operation_state.hpp"
#include "is_nothrow_connectable.hpp"
#include "storage_for_completion_signatures.hpp"

namespace exec {

struct get_uninitialized_storage_t {
  template<typename Env>
    requires requires (const Env e, const get_uninitialized_storage_t& self) {
      //  TODO: Require a pointer? (note ::stdexec::prop seems to make a
      //  reference to a pointer)
      { e.query(self) } noexcept;
    }
  constexpr void* operator()(const Env& e) const noexcept {
    return e.query(*this);
  }
};

constexpr inline get_uninitialized_storage_t get_uninitialized_storage;

struct get_initialized_storage_t {
  template<typename Env>
    requires requires (const Env e, const get_initialized_storage_t& self) {
      //  TODO: Require a pointer?
      { e.query(self) } noexcept;
    }
  constexpr auto operator()(const Env& e) const noexcept {
    return e.query(*this);
  }
};

constexpr inline get_initialized_storage_t get_initialized_storage;

template<std::size_t N>
struct get_object_t {
  template<typename Env>
    requires requires (const Env e, const get_object_t& self) {
      //  TODO: Require a reference?
      { e.query(self) } noexcept;
    }
  constexpr decltype(auto) operator()(const Env& e) const noexcept {
    return e.query(*this);
  }
};

template<std::size_t N>
constexpr inline get_object_t<N> get_object;

namespace detail::with {

struct on_stop_request {
  constexpr explicit on_stop_request(::stdexec::inplace_stop_source& source)
    noexcept : source_(source)
  {}
  void operator()() && noexcept {
    source_.request_stop();
  }
private:
  ::stdexec::inplace_stop_source& source_;
};

template<typename Env>
using stop_callback = ::stdexec::stop_callback_for_t<
  ::stdexec::stop_token_of_t<Env>,
  on_stop_request>;

template<typename>
class storage;

template<typename T>
  requires std::is_object_v<T>
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
  template<typename Env>
  constexpr auto get_construct_env(Env e) noexcept(
    std::is_nothrow_move_constructible_v<Env>)
  {
    auto tag = ::exec::get_uninitialized_storage;
    auto ptr = get_uninitialized_storage();
    auto prop = ::stdexec::prop(tag, ptr);
    return ::stdexec::env(
      prop,
      std::move(e));
    //  This alternate code for this function:
    //
    //  return ::stdexec::env(
    //    ::stdexec::prop(
    //      ::exec::get_uninitialized_storage,
    //      get_uninitialized_storage()),
    //    std::move(e));
    //
    //  Causes ASan to report a "stack-use-after-return" on Clang 18
  }
  template<typename Env>
  constexpr auto get_destroy_env(Env e) noexcept(
    std::is_nothrow_move_constructible_v<Env>)
  {
    auto tag = ::exec::get_initialized_storage;
    auto ptr = get_initialized_storage();
    auto prop = ::stdexec::prop(tag, ptr);
    return ::stdexec::env(
      prop,
      std::move(e));
    //  This function has the same problem as above with this alternate code:
    //
    //  return ::stdexec::env(
    //    ::stdexec::prop(
    //      ::exec::get_initialized_storage,
    //      get_initialized_storage()),
    //    std::move(e));
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

template<typename Object, typename T>
  requires requires(Object object, storage<T> storage) {
    { object.construct(storage.get_uninitialized_storage()) } -> ::stdexec::sender;
  }
constexpr auto get_constructor(Object& object, storage<T>& storage)
  noexcept(noexcept(object.construct(storage.get_uninitialized_storage())))
{
  return object.construct(storage.get_uninitialized_storage());
}

template<typename Object, typename T>
  requires requires(Object object) {
    { object.construct() } -> ::stdexec::sender;
  }
constexpr auto get_constructor(Object& object, storage<T>&) noexcept(
  noexcept(object.construct()))
{
  return object.construct();
}

template<typename Object, typename T>
  requires requires(Object object, storage<T> storage) {
    { ((Object&&)object).destroy(storage.get_initialized_storage()) } noexcept -> ::stdexec::sender;
  }
constexpr auto get_destructor(Object&& object, storage<T>& storage) noexcept {
  return ((Object&&)object).destroy(storage.get_initialized_storage());
}

template<typename Object, typename T>
  requires requires(Object object) {
    { ((Object&&)object).destroy() } noexcept -> ::stdexec::sender;
  }
constexpr auto get_destructor(Object&& object, storage<T>&) noexcept {
  return ((Object&&)object).destroy();
}

template<typename Object>
struct type_from_object {
  using type = void;
};

template<typename Object>
concept has_type = requires {
  typename Object::type;
};

template<typename Object>
  requires has_type<Object>
struct type_from_object<Object>;

template<typename Object>
  requires
    has_type<Object> &&
    std::is_object_v<typename Object::type>
struct type_from_object<Object> {
  using type = typename Object::type;
};

template<typename Env>
struct constructor_archetype_receiver_base {
  using receiver_concept = ::stdexec::receiver_t;
  Env get_env() const noexcept;
  template<typename T>
  void set_error(T&&) && noexcept;
  void set_stopped() && noexcept;
};
static_assert(::stdexec::receiver<
  constructor_archetype_receiver_base<::stdexec::env<>>>);

template<typename Env>
struct constructor_archetype_receiver : constructor_archetype_receiver_base<Env>
{
  void set_value() && noexcept;
};
static_assert(::stdexec::receiver<
  constructor_archetype_receiver<::stdexec::env<>>>);

template<typename Sender, typename Env>
concept constructor_in =
  !::stdexec::sender_to<Sender, constructor_archetype_receiver_base<Env>> &&
  ::stdexec::sender_to<Sender, constructor_archetype_receiver<Env>>;

template<typename Env>
struct destructor_archetype_receiver_base {
  using receiver_concept = ::stdexec::receiver_t;
  Env get_env() const noexcept;
};
static_assert(::stdexec::receiver<
  destructor_archetype_receiver_base<::stdexec::env<>>>);

template<typename Env>
struct destructor_archetype_receiver : destructor_archetype_receiver_base<Env> {
  void set_value() && noexcept;
};
static_assert(::stdexec::receiver<
  destructor_archetype_receiver<::stdexec::env<>>>);

template<typename Sender, typename Env>
concept destructor_in =
  !::stdexec::sender_to<Sender, destructor_archetype_receiver_base<Env>> &&
  ::stdexec::sender_to<Sender, destructor_archetype_receiver<Env>> &&
  requires(Sender s, destructor_archetype_receiver<Env> r) {
    { ::stdexec::connect((Sender&&)s, (destructor_archetype_receiver<Env>&&)r) }
      noexcept;
  };

struct dummy_prop {};

template<
  typename Derived,
  typename Env,
  typename Tag,
  typename Object>
class object_state {
private:
  static_assert(std::is_nothrow_move_constructible_v<Env>);
  using type_ = typename type_from_object<Object>::type;
  using storage_type_ = storage<type_>;
  static constexpr bool has_get_object_ = requires(storage_type_ storage) {
    storage.get_object();
  };
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
      self_.complete_construct_();
      self_.get_derived_().constructed(Tag{});
    }
    template<typename... Args>
    constexpr void set_error(Args&&... args) && noexcept {
      self_.complete_construct_();
      self_.get_derived_().construct_error(Tag{}, (Args&&)args...);
    }
    template<typename... Args>
    constexpr void set_stopped(Args&&... args) && noexcept {
      self_.complete_construct_();
      self_.get_derived_().construct_stopped(Tag{}, (Args&&)args...);
    }
    constexpr construct_env_ get_env() const noexcept {
      return self_.object_storage_.get_construct_env(self_.get_env_());
    }
    template<typename ChildOp>
    constexpr static construct_receiver_ make_receiver_for(ChildOp* ptr) noexcept {
      static_assert(std::is_same_v<construct_operation_state_, ChildOp>);
      return construct_receiver_(
        //  TODO: This is UB
        *std::launder(
          reinterpret_cast<object_state>(ptr)));
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
  static_assert(::stdexec::inlinable_receiver<
    construct_receiver_,
    construct_operation_state_>);
  static_assert(
    std::is_same_v<
      construct_env_,
      ::stdexec::env_of_t<construct_receiver_>>);
  struct destroy_receiver_ {
    using receiver_concept = ::stdexec::receiver_t;
    constexpr explicit destroy_receiver_(object_state& self) noexcept
      : self_(self)
    {}
    constexpr void set_value() && noexcept {
      self_.complete_destroy_();
      self_.get_derived_().destroyed(Tag{});
    }
    constexpr destroy_env_ get_env() const noexcept {
      return self_.object_storage_.get_destroy_env(self_.get_env_());
    }
    template<typename ChildOp>
    constexpr static destroy_receiver_ make_receiver_for(ChildOp* ptr) noexcept {
      static_assert(std::is_same_v<destroy_operation_state_, ChildOp>);
      return destroy_receiver_(
        //  TODO: This is UB
        *std::launder(
          reinterpret_cast<object_state>(ptr)));
    }
  private:
    object_state& self_;
  };
  using destructor_ = decltype(
    with::get_destructor(
      std::declval<Object>(),
      std::declval<storage_type_&>()));
  using destroy_operation_state_ = ::stdexec::connect_result_t<
    destructor_,
    destroy_receiver_>;
  static_assert(::stdexec::inlinable_receiver<destroy_receiver_, destroy_operation_state_>);
  static_assert(
    noexcept(
      ::stdexec::connect(
        std::declval<destructor_>(),
        std::declval<destroy_receiver_>())));
  static_assert(
    std::is_same_v<
      destroy_env_,
      ::stdexec::env_of_t<destroy_receiver_>>);
  static constexpr auto align_ = std::max(
    alignof(construct_operation_state_),
    alignof(destroy_operation_state_));
  static constexpr auto size_ = std::max(
    sizeof(construct_operation_state_),
    sizeof(destroy_operation_state_));
  alignas(align_) std::byte operation_state_storage_[size_];
  storage_type_ object_storage_;
  Object object_;
  constexpr construct_operation_state_* get_construct_operation_state_() noexcept {
    return std::launder(
      reinterpret_cast<construct_operation_state_*>(
        operation_state_storage_));
  }
  constexpr destroy_operation_state_* get_destroy_operation_state_() noexcept {
    return std::launder(
      reinterpret_cast<destroy_operation_state_*>(
        operation_state_storage_));
  }
  constexpr void complete_construct_() noexcept {
    get_construct_operation_state_()->~construct_operation_state_();
  }
  constexpr void complete_destroy_() noexcept {
    get_destroy_operation_state_()->~destroy_operation_state_();
  }
public:
  object_state(const object_state&) = delete;
  object_state& operator=(const object_state&) = delete;
  constexpr object_state(Object o) noexcept(
    std::is_nothrow_move_constructible_v<Object>)
    : object_((Object&&)o)
  {}
  constexpr void connect_construct() noexcept(
    noexcept(
      ::stdexec::connect(
        with::get_constructor(
          std::declval<Object&>(),
          std::declval<storage_type_&>()),
        std::declval<construct_receiver_>())))
  {
    new(operation_state_storage_) construct_operation_state_(
      ::stdexec::connect(
        with::get_constructor(object_, object_storage_),
        construct_receiver_(*this)));
  }
  constexpr void destroy_construct() noexcept {
    complete_construct_();
  }
  constexpr void start_construct() noexcept {
    ::stdexec::start(*get_construct_operation_state_());
  }
  constexpr void connect_destroy() noexcept {
    new(operation_state_storage_) destroy_operation_state_(
      ::stdexec::connect(
        with::get_destructor((Object&&)object_, object_storage_),
        destroy_receiver_(*this)));
  }
  constexpr void start_destroy() noexcept {
    ::stdexec::start(*get_destroy_operation_state_());
  }
  constexpr decltype(auto) get_object() noexcept requires has_get_object_ {
    return object_storage_.get_object();
  }
  template<std::size_t N>
  constexpr dummy_prop get_prop() noexcept {
    return {};
  }
  template<std::size_t N>
  constexpr auto get_prop() noexcept requires has_get_object_ {
    return ::stdexec::prop<
      ::exec::get_object_t<N>,
      decltype(get_object())>(
        ::exec::get_object<N>,
        get_object());
  }
};

template<typename, typename, typename, typename...>
struct object_states;

template<std::size_t>
struct object_tag {};

template<typename T, std::size_t N, typename Object>
struct prop_impl {
  using type = ::stdexec::prop<
    ::exec::get_object_t<N>,
    T&>;
};

template<std::size_t N, typename Object>
struct prop_impl<void, N, Object> {
  using type = dummy_prop;
};

template<std::size_t N, typename Object>
using prop = typename prop_impl<
  typename type_from_object<Object>::type,
  N,
  Object>::type;

template<typename, typename, typename...>
struct env_impl;

template<typename Env, std::size_t... Ns, typename... Args>
struct env_impl<
  Env,
  std::index_sequence<Ns...>,
  Args...>
{
  using type = ::stdexec::env<
    prop<Ns, Args>...,
    Env>;
};

template<typename Env, typename... Objects>
using env = typename env_impl<
  Env,
  std::index_sequence_for<Objects...>,
  Objects...>::type;

template<
  typename Derived,
  typename Env,
  std::size_t... Indices,
  typename... Objects>
class object_states<Derived, Env, std::index_sequence<Indices...>, Objects...> :
  public object_state<Derived, Env, object_tag<Indices>, Objects>...
{
  template<std::size_t I, typename Object>
  using base_ = object_state<Derived, Env, object_tag<I>, Object>;
public:
  template<typename... Args>
    requires (std::is_constructible_v<base_<Indices, Objects>, Args&&> && ...)
  explicit constexpr object_states(Args&&... args) noexcept(
    (std::is_nothrow_constructible_v<base_<Indices, Objects>, Args&&> && ...))
    : base_<Indices, Objects>((Args&&)args)...
  {}
  template<std::size_t N>
    requires (N < sizeof...(Objects))
  constexpr decltype(auto) get() noexcept {
    using type = std::tuple<base_<Indices, Objects>...>;
    using nth = std::tuple_element_t<N, type>;
    nth& retr = *this;
    return retr;
  }
  using env_type = env<Env, Objects...>;
  constexpr env_type get_env(Env e) noexcept {
    return env_type{
      get<Indices>().template get_prop<Indices>()...,
      std::move(e)};
  }
  template<typename Visitor>
  constexpr void for_each(Visitor&& v) noexcept(
    (std::is_nothrow_invocable_v<
      Visitor&,
      base_<Indices, Objects>&> && ...))
  {
    ((void)std::invoke(v, get<Indices>()), ...);
  }
};

template<typename...>
using remove_set_value = ::stdexec::completion_signatures<>;

template<typename Sender, typename Env>
using constructor_completion_signatures =
  ::stdexec::transform_completion_signatures<
    ::stdexec::completion_signatures_of_t<
      Sender,
      Env>,
    ::stdexec::completion_signatures<>,
    remove_set_value>;

template<typename Object>
using storage_for = storage<
  typename type_from_object<Object>::type>;

template<typename Object, typename Env>
using construct_env = decltype(
  std::declval<storage_for<Object>&>().get_construct_env(
    std::declval<Env>()));

template<typename Object>
using constructor = decltype(
  with::get_constructor(
    std::declval<Object&>(),
    std::declval<storage_for<Object>&>()));

template<typename Object, typename Env>
using object_completion_signatures = constructor_completion_signatures<
  constructor<Object>,
  construct_env<Object, Env>>;
  //::stdexec::transform_completion_signatures<
  //  constructor_completion_signatures<
  //    constructor<Object>,
  //    construct_env<Object, Env>>,
  //  std::conditional_t<
  //    noexcept(
  //      with::get_constructor(
  //        std::declval<Object&>(),
  //        std::declval<storage_for<Object>&>()))/* &&
  //    ::exec::is_nothrow_connectable_v<
  //      constructor<Object>,
  //      construct_env<Object, Env>>*/,
  //    ::stdexec::completion_signatures<>,
  //    ::stdexec::completion_signatures<
  //      ::stdexec::set_error_t(std::exception_ptr)>>>;

template<typename, typename...>
struct objects_completion_signatures {
  using type = ::stdexec::completion_signatures<>;
};
template<typename Env, typename Object, typename... Objects>
struct objects_completion_signatures<Env, Object, Objects...> {
  using type = ::stdexec::transform_completion_signatures<
    object_completion_signatures<Object, Env>,
    typename objects_completion_signatures<Env, Objects...>::type>;
};

template<typename Sender, typename Env, typename... Objects>
using main_completion_signatures =
  ::stdexec::transform_completion_signatures<
    ::stdexec::completion_signatures_of_t<
      Sender,
      env<Env, Objects...>>,
    std::conditional_t<
      ::exec::is_nothrow_connectable_v<
        Sender,
        env<Env, Objects...>>,
      ::stdexec::completion_signatures<>,
      ::stdexec::completion_signatures<
        ::stdexec::set_error_t(std::exception_ptr)>>>;

struct tag {};

template<typename Env>
using stop_source_env = ::stdexec::env<
  ::stdexec::prop<
    ::stdexec::get_stop_token_t,
    ::stdexec::inplace_stop_token>,
  Env>;

template<typename Sender, typename Env, typename... Objects>
using storage_for_completion_signatures =
  ::exec::storage_for_completion_signatures<
    ::stdexec::transform_completion_signatures<
      main_completion_signatures<Sender, Env, Objects...>,
      typename objects_completion_signatures<Env, Objects...>::type>>;

template<typename Sender, typename Receiver, typename... Objects>
class operation_state :
  public exec::inlinable_operation_state<
    operation_state<Sender, Receiver, Objects...>,
    Receiver>,
  public object_states<
    operation_state<Sender, Receiver, Objects...>,
    stop_source_env<::stdexec::env_of_t<Receiver>>,
    std::index_sequence_for<Objects...>,
    Objects...>,
  public manual_child_operation_state<
    operation_state<Sender, Receiver, Objects...>,
    tag,
    typename object_states<
      operation_state<Sender, Receiver, Objects...>,
      stop_source_env<::stdexec::env_of_t<Receiver>>,
      std::index_sequence_for<Objects...>,
      Objects...>::env_type,
    Sender>
{
  using base_ = exec::inlinable_operation_state<
    operation_state,
    Receiver>;
  using base_::get_receiver;
  using receiver_env_ = ::stdexec::env_of_t<Receiver>;
  using env_ = stop_source_env<receiver_env_>;
  using object_states_base_ = object_states<
    operation_state,
    env_,
    std::index_sequence_for<Objects...>,
    Objects...>;
  using child_base_ = manual_child_operation_state<
    operation_state,
    tag,
    typename object_states_base_::env_type,
    Sender>;
  using storage_for_completion_signatures_ = storage_for_completion_signatures<
    Sender,
    env_,
    Objects...>;
  enum struct phase_ {
    uninitialized,
    constructing,
    constructed,
    destroying
  };
  constexpr bool decrement_() noexcept {
    return outstanding_.fetch_sub(1, std::memory_order_acq_rel) == 1;
  }
  constexpr void destroy_() noexcept {
    assert(!outstanding_.load(std::memory_order_relaxed));
    outstanding_.store(sizeof...(Objects), std::memory_order_relaxed);
    object_states_base_::for_each([&](auto&& state) noexcept {
      state.connect_destroy();
      state.start_destroy();
    });
  }
  constexpr void destroy_some_() noexcept {
    STDEXEC_ASSERT(arrived_.load(std::memory_order_relaxed));
    object_states_base_::for_each([&]<std::size_t N, typename Object>(
      object_state<operation_state, env_, object_tag<N>, Object>& state)
        noexcept
    {
      {
        auto expected = phase_::constructed;
        if (phases_[N].compare_exchange_strong(
          expected,
          phase_::destroying,
          std::memory_order_relaxed))
        {
          //  We found that the object had been constructed and managed to
          //  claim responsibility for starting the destructor
          state.connect_destroy();
          outstanding_.fetch_add(1, std::memory_order_relaxed);
          state.start_destroy();
        }
      }
    });
  }
  constexpr void store_current_exception_() noexcept {
    storage_.arrive(
      ::stdexec::set_error,
      std::current_exception());
  }
  template<typename... Args>
  constexpr bool maybe_store_(Args&&... args) noexcept {
    if (!arrived_.exchange(true, std::memory_order_relaxed)) {
      STDEXEC_ASSERT(stop_callback_);
      stop_callback_.reset();
      stop_source_.request_stop();
      storage_.arrive((Args&&)args...);
      return true;
    }
    return false;
  }
  template<typename... Args>
  constexpr void complete_(Args&&... args) noexcept {
    storage_.arrive((Args&&)args...);
    destroy_();
  }
  template<std::size_t N, typename... Args>
  constexpr void construct_complete_(Args&&... args) noexcept {
    assert(phases_[N].load(std::memory_order_relaxed) == phase_::constructing);
    phases_[N].store(phase_::uninitialized, std::memory_order_relaxed);
    if (maybe_store_((Args&&)args...)) {
      destroy_some_();
    }
    maybe_finalize_();
  }
  constexpr void finalize_() noexcept {
    ((storage_for_completion_signatures_&&)storage_).complete(
      (Receiver&&)get_receiver());
  }
  constexpr void maybe_finalize_() noexcept {
    if (decrement_()) {
      finalize_();
    }
  }
  constexpr void maybe_destroy_construct_() noexcept {
    object_states_base_::for_each([&]<std::size_t N, typename Object>(
      object_state<operation_state, env_, object_tag<N>, Object>& state)
        noexcept
    {
      if (phases_[N].load(std::memory_order_relaxed) ==
        phase_::constructing)
      {
        state.destroy_construct();
      }
    });
  }
  std::array<std::atomic<phase_>, sizeof...(Objects)> phases_{};
  std::atomic<std::size_t> outstanding_{sizeof...(Objects)};
  std::atomic<bool> arrived_{false};
  Sender s_;
  storage_for_completion_signatures_ storage_;
  //  Future enhancements, these are unnecessary if:
  //
  //  - All constructors can't stop or fail
  //  - No constructor can send set_stopped when parameterized on an environment
  //    with the right stop token type
  ::stdexec::inplace_stop_source stop_source_;
  std::optional<
    stop_callback<
      receiver_env_>> stop_callback_;
public:
  template<typename... Args>
    requires (std::is_constructible_v<Objects, Args&&> && ...)
  explicit constexpr operation_state(
    Sender s,
    Receiver r,
    Args&&... args) noexcept(
      std::is_nothrow_move_constructible_v<Sender> &&
      (
        (
          std::is_nothrow_constructible_v<Objects, Args&&> &&
          noexcept(
            with::get_constructor(
              std::declval<Objects&>(),
              std::declval<storage_for<Objects>&>())) &&
          ::exec::is_nothrow_connectable_v<
            constructor<Objects>,
            construct_env<Objects, env_>>) && ...))
    : base_((Receiver&&)r),
      object_states_base_((Args&&)args...),
      s_((Sender&&)s)
  {
    const auto impl = [&]<std::size_t N, typename Object>(
      object_state<operation_state, env_, object_tag<N>, Object>& state)
        noexcept(noexcept(state.connect_construct()))
    {
      assert(phases_[N].load(std::memory_order_relaxed) ==
        phase_::uninitialized);
      state.connect_construct();
      phases_[N].store(phase_::constructing, std::memory_order_relaxed);
    };
    if constexpr (noexcept(object_states_base_::for_each(impl))) {
      object_states_base_::for_each(impl);
    } else {
      try {
        object_states_base_::for_each(impl);
      } catch (...) {
        maybe_destroy_construct_();
        throw;
      }
    }
  }
  //  inplace_stop_source means no constexpr per Clang 18
  /*constexpr*/ ~operation_state() noexcept {
    maybe_destroy_construct_();
  }
  constexpr void start() & noexcept {
    //  TODO: Account for throwing
    stop_callback_.emplace(
      ::stdexec::get_stop_token(
        ::stdexec::get_env(
          get_receiver())),
      on_stop_request(stop_source_));
    object_states_base_::for_each([&](auto&& state) noexcept {
      state.start_construct();
    });
  }
  template<std::size_t N>
  constexpr void constructed(object_tag<N>) noexcept {
    assert(phases_[N].load(std::memory_order_relaxed) == phase_::constructing);
    phases_[N].store(phase_::constructed, std::memory_order_relaxed);
    if (!arrived_.load(std::memory_order_relaxed)) {
      if (!decrement_()) {
        //  Waiting on someone else to finish constructing
        return;
      }
      STDEXEC_ASSERT(stop_callback_);
      stop_callback_.reset();
      constexpr auto nothrow = noexcept(child_base_::construct(std::move(s_)));
      const auto impl = [&]() noexcept(nothrow) {
        child_base_::construct(std::move(s_));
        child_base_::start();
      };
      if constexpr (nothrow) {
        impl();
      } else {
        try {
          impl();
        } catch (...) {
          store_current_exception_();
          destroy_();
        }
      }
      return;
    }
    //  We're doomed, now we need to figure out if we should start tearing
    //  ourself down
    if (
      auto expected = phase_::constructed;
      !phases_[N].compare_exchange_strong(
        expected,
        phase_::destroying,
        std::memory_order_relaxed))
    {
      //  Someone else started tearing us down but we still need to decrement
      //  and since we need to decrement we could find ourselves last in and
      //  need to finalize
      maybe_finalize_();
      return;
    }
    //  Since we managed to replace constructed with destroying it means the
    //  onus is on us to start our destructor
    auto&& state = object_states_base_::template get<N>();
    state.connect_destroy();
    state.start_destroy();
  }
  template<std::size_t N>
  constexpr void destroyed(object_tag<N>) noexcept {
    maybe_finalize_();
  }
  template<std::size_t N, typename... Args>
  constexpr void construct_error(object_tag<N>, Args&&... args) noexcept {
    construct_complete_<N>(::stdexec::set_error, (Args&&)args...);
  }
  template<std::size_t N, typename... Args>
  constexpr void construct_stopped(object_tag<N>, Args&&... args) noexcept {
    construct_complete_<N>(::stdexec::set_stopped, (Args&&)args...);
  }
  //  Not constexpr due to inplace_stop_token per Clang 18
  /*constexpr*/ env_ get_env() noexcept {
    auto tag = ::stdexec::get_stop_token;
    auto token = stop_source_.get_token();
    auto prop = ::stdexec::prop(
      (::stdexec::get_stop_token_t&&)tag,
      (decltype(token)&&)token);
    auto env = ::stdexec::get_env(get_receiver());
    return ::stdexec::env(
      (decltype(prop)&&)prop,
      (receiver_env_&&)env);
    //  The code below ICEs Clang 18:
    //
    //  return ::stdexec::env(
    //    ::stdexec::prop(
    //      ::stdexec::get_stop_token,
    //      stop_source_.get_token()),
    //    ::stdexec::get_env(
    //      get_receiver()));
  }
  constexpr auto get_env(tag) noexcept {
    return object_states_base_::get_env(get_env());
  }
  template<typename... Args>
  constexpr void set_value(tag, Args&&... args) noexcept {
    complete_(::stdexec::set_value, (Args&&)args...);
  }
  template<typename... Args>
  constexpr void set_error(tag, Args&&... args) noexcept {
    complete_(::stdexec::set_error, (Args&&)args...);
  }
  template<typename... Args>
  constexpr void set_stopped(tag, Args&&... args) noexcept {
    complete_(::stdexec::set_stopped, (Args&&)args...);
  }
};

template<typename Sender, typename Env, typename... Objects>
using completion_signatures = typename storage_for_completion_signatures<
  Sender,
  Env,
  Objects...>::completion_signatures;

}

template<typename T, typename Env>
concept object_in =
  std::is_move_constructible_v<T> &&
  requires(
    T o,
    detail::with::storage<typename detail::with::type_from_object<T>::type> s,
    Env e)
  {
    { detail::with::get_constructor(o, s) } ->
      detail::with::constructor_in<
        decltype(s.get_construct_env((Env&&)e))>;
    { detail::with::get_destructor((T&&)o, s) } ->
      detail::with::destructor_in<
        decltype(s.get_destroy_env((Env&&)e))>;
  };

namespace detail::with {

template<typename Sender, typename... Objects>
struct sender {
  Sender s_;
  std::tuple<Objects...> objects_;
  template<typename Receiver>
  using operation_state_ = operation_state<
    Sender,
    Receiver,
    Objects...>;
  template<typename Env>
  using env_ = detail::with::stop_source_env<Env>;
public:
  using sender_concept = ::stdexec::sender_t;
  template<typename S, typename... Os>
    requires
      std::is_constructible_v<Sender, S&&> &&
      (std::is_constructible_v<Objects, Os&&> && ...)
  explicit constexpr sender(S&& s, Os&&... os) noexcept(
    std::is_nothrow_constructible_v<Sender, S&&> &&
    (std::is_nothrow_constructible_v<Objects, Os&&> && ...))
    : s_((S&&)s),
      objects_((Os&&)os...)
  {}
  template<typename Env>
    requires
      std::is_copy_constructible_v<Sender> &&
      (std::is_copy_constructible_v<Objects> && ...) &&
      (object_in<Objects, env_<Env>> && ...)
  consteval completion_signatures<
    Sender,
    env_<Env>,
    Objects...> get_completion_signatures(const Env&) const& noexcept
  {
    return {};
  }
  template<typename Env>
    requires (object_in<Objects, env_<Env>> && ...)
  consteval completion_signatures<
    Sender,
    env_<Env>,
    Objects...> get_completion_signatures(const Env&) && noexcept
  {
    return {};
  }
  template<typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        const sender&,
        ::stdexec::env_of_t<Receiver>>>
  constexpr auto connect(Receiver r) const& noexcept(
    std::is_nothrow_constructible_v<
      operation_state_<Receiver>,
      const Sender&,
      Receiver&&,
      const Objects&...>)
  {
    using t = operation_state_<Receiver>;
    return std::apply(
      [&](auto&&... objects) noexcept(
        std::is_nothrow_constructible_v<
          t,
          const Sender&,
          Receiver&&,
          decltype(objects)&&...>)
      {
        return t(s_, (Receiver&&)r, (decltype(objects)&&)objects...);
      },
      objects_);
  }
  template<typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        sender,
        ::stdexec::env_of_t<Receiver>>>
  constexpr auto connect(Receiver r) && noexcept(
    std::is_nothrow_constructible_v<
      operation_state_<Receiver>,
      Sender&&,
      Receiver&&,
      Objects&&...>)
  {
    using t = operation_state_<Receiver>;
    return std::apply(
      [&](auto&&... objects) noexcept(
        std::is_nothrow_constructible_v<
          t,
          Sender&&,
          Receiver&&,
          decltype(objects)&&...>)
      {
        return t((Sender&&)s_, (Receiver&&)r, (decltype(objects)&&)objects...);
      },
      (std::tuple<Objects...>&&)objects_);
  }
};

}

template<typename Sender, typename... Objects>
  requires
    ::stdexec::sender<Sender> &&
    (std::is_move_constructible_v<Objects> && ...)
constexpr detail::with::sender<Sender, Objects...> with(
  Sender s,
  Objects... os) noexcept(
    std::is_nothrow_constructible_v<
      detail::with::sender<Sender, Objects...>,
      Sender,
      Objects...>)
{
  return detail::with::sender<Sender, Objects...>(
    (Sender&&)s,
    (Objects&&)os...);
}

} // namespace exec
