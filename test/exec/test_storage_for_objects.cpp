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

#include <exec/storage_for_objects.hpp>

#include <catch2/catch.hpp>
#include <exec/object.hpp>
#include <stdexec/execution.hpp>

#include <cstddef>
#include <new>
#include <tuple>

#include "../test_common/receivers.hpp"

namespace {

struct int_object {
  using type = int;
  ::stdexec::sender auto construct(void* const ptr) noexcept {
    return ::stdexec::just(ptr) | ::stdexec::then([](void* const ptr) noexcept {
      new(ptr) int(5);
    });
  }
  ::stdexec::sender auto destroy(int* const ptr) noexcept {
    return ::stdexec::just(ptr) | ::stdexec::then([](int* const ptr) noexcept {
      *ptr = 6;
    });
  }
};
static_assert(::exec::object<int_object>);

TEST_CASE("A single object can be constructed and destroyed", "[construct]") {
  ::exec::storage_for_objects<int_object> storage;
  //  This ensures we can check that the ctor hasn't run
  new(storage.get<0>().get_uninitialized()) int(0);
  int_object o;
  auto op = ::stdexec::connect(
    storage.construct(o),
    expect_void_receiver{});
  CHECK(*storage.get<0>().get_initialized() == 0);
  CHECK(std::get<0>(storage.get_arguments()) == 0);
  ::stdexec::start(op);
  CHECK(*storage.get<0>().get_initialized() == 5);
  CHECK(std::get<0>(storage.get_arguments()) == 5);
  {
    std::size_t invoked = 0;
    const auto f = [&](const int i) noexcept {
      ++invoked;
      CHECK(i == 5);
    };
    static_assert(noexcept(storage(f)));
    storage(f);
    CHECK(invoked == 1);
  }
  {
    std::size_t invoked = 0;
    const auto f = [&](const int i) {
      ++invoked;
      CHECK(i == 5);
    };
    static_assert(!noexcept(storage(f)));
    storage(f);
    CHECK(invoked == 1);
  }
  //  TODO: Destroy
}

} // unnamed namespace
