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

#include <exec/object.hpp>

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>

#include <new>

namespace {

static_assert(
  ::exec::constructor_in<
    decltype(::stdexec::just()),
    ::stdexec::env<>>);
static_assert(
  !::exec::constructor_in<
    decltype(::stdexec::just(5)),
    ::stdexec::env<>>);

static_assert(
  ::exec::destructor_in<
    decltype(::stdexec::just()),
    ::stdexec::env<>>);
static_assert(
  !::exec::destructor_in<
    decltype(::stdexec::just(5)),
    ::stdexec::env<>>);

static_assert(!::exec::object<int>);

struct int_object {
  using type = int;
  auto construct(void* ptr) {
    return ::stdexec::just(ptr) | ::stdexec::then([](void* ptr) noexcept {
      new(ptr) int(5);
    });
  }
  auto destroy(int* ptr) noexcept {
    return ::stdexec::just(ptr) | ::stdexec::then([](int* ptr) noexcept {
      (void)ptr;
    });
  };
};

static_assert(::exec::object<int_object>);
static_assert(::exec::object_in<int_object, ::stdexec::env<>>);
static_assert(!::exec::void_object<int_object>);

struct void_object {
  auto construct() {
    return ::stdexec::just();
  }
  auto destroy() noexcept {
    return ::stdexec::just();
  };
};

static_assert(::exec::object<void_object>);
static_assert(::exec::object_in<void_object, ::stdexec::env<>>);
static_assert(::exec::void_object<void_object>);

struct void_object_with_type_alias : void_object {
  using type = void;
};

static_assert(::exec::object<void_object_with_type_alias>);
static_assert(::exec::object_in<void_object_with_type_alias, ::stdexec::env<>>);
static_assert(::exec::void_object<void_object_with_type_alias>);

} // unnamed namespace
