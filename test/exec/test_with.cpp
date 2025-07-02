/*
 * Copyright (c) Facebook, Inc. and its affiliates.
 * Copyright (c) 2021-2022 NVIDIA Corporation
 *
 * Licensed under the Apache License Version 2.0 with LLVM Exceptions
 * (the "License"); you may not use this file except in compliance with
 * the License. You may obtain a copy of the License at
 *
 *   https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <exec/with.hpp>

#include <catch2/catch.hpp>
#include <stdexec/execution.hpp>

//#  include "../test_common/require_terminate.hpp"
//#  include "../test_common/schedulers.hpp"

using namespace exec;

namespace {

TEST_CASE("Constructor and destructor are obtained from an object which "
  "accepts pointers directly as arguments to its sender factories",
  "[with][detail]")
{
  struct object {
    auto construct(void*) {
      return ::stdexec::just();
    }
    auto destroy(int*) noexcept {
      return ::stdexec::just();
    }
  };
  object o;
  detail::with::storage<int> storage;
  (void)detail::with::get_constructor(o, storage);
  (void)detail::with::get_destructor(o, storage);
}

TEST_CASE("Constructor and destructor are obtained from an object which "
  "does not accept pointers as arguments to its sender factories",
  "[with][detail]")
{
  struct object {
    auto construct() {
      return ::stdexec::just();
    }
    auto destroy() noexcept {
      return ::stdexec::just();
    }
  };
  object o;
  detail::with::storage<int> storage;
  (void)detail::with::get_constructor(o, storage);
  (void)detail::with::get_destructor(o, storage);
}

TEST_CASE("Constructor and destructor are obtained from an object when the "
  "storage stores void",
  "[with][detail]")
{
  struct object {
    auto construct() {
      return ::stdexec::just();
    }
    auto destroy() noexcept {
      return ::stdexec::just();
    }
  };
  object o;
  detail::with::storage<void> storage;
  (void)detail::with::get_constructor(o, storage);
  (void)detail::with::get_destructor(o, storage);
}

TEST_CASE("Per object state functions as expected when given simple senders "
  "which do not use the environment", "[with][detail]")
{
  //  TODO
}

} // unnamed namespace
