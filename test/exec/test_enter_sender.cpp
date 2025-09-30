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

#include <exec/enter_sender.hpp>

#include <catch2/catch.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

namespace {

static_assert(
  !::exec::enter_sender_in<
    decltype(::stdexec::just()),
    ::stdexec::env<>>);
static_assert(
  ::exec::enter_sender_in<
    decltype(::stdexec::just(::stdexec::just())),
    ::stdexec::env<>>);

static_assert(
  std::is_same_v<
    decltype(::stdexec::just()),
    ::exec::exit_sender_of_t<
      decltype(::stdexec::just(::stdexec::just())),
      ::stdexec::env<>>>);
static_assert(
  std::is_same_v<
    decltype(::stdexec::just()),
    ::exec::exit_sender_of_t<
      ::exec::variant_sender<
        decltype(::stdexec::just(::stdexec::just())),
        decltype(::stdexec::just_stopped())>,
      ::stdexec::env<>>>);

} // unnamed namespace
