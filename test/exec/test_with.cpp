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

#include <cstddef>
#include <memory>
#include <new>

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
  struct state {
    std::size_t construct{0};
    std::size_t destroy{0};
  };
  //  Use count allows us to instrument leaks
  std::shared_ptr<void> ptr(std::make_shared<int>(5));
  struct object {
    using type = int;
    auto construct(void* storage) {
      return
        ::stdexec::just(ptr) |
        ::stdexec::then([this, storage](auto&&) {
          ++s.construct;
          new(storage) int(5);
        });
    }
    auto destroy() && noexcept {
      return
        ::stdexec::just(ptr) |
        ::stdexec::then([this](auto&&) noexcept {
          ++s.destroy;
        });
    }
    state& s;
    std::shared_ptr<void>& ptr;
  };
  class derived : public detail::with::object_state<
    derived,
    ::stdexec::env<>,
    0,
    object>
  {
    using base_ = detail::with::object_state<
      derived,
      ::stdexec::env<>,
      0,
      object>;
  public:
    using base_::base_;
    template<std::size_t N>
    void constructed() noexcept {
      //  TODO
    }
    template<std::size_t N>
    void destroyed() noexcept {
      //  TODO
    }
  };
  state s;
  {
    derived d(object{s, ptr});
    CHECK(ptr.use_count() == 1U);
    CHECK(s.construct == 0U);
    CHECK(s.destroy == 0U);
    d.connect_construct();
    CHECK(ptr.use_count() == 2U);
    CHECK(s.construct == 0U);
    CHECK(s.destroy == 0U);
    d.start_construct();
    CHECK(ptr.use_count() == 1U);
    CHECK(s.construct == 1U);
    CHECK(s.destroy == 0U);
    d.connect_destroy();
    CHECK(ptr.use_count() == 2U);
    CHECK(s.construct == 1U);
    CHECK(s.destroy == 0U);
    d.start_destroy();
    CHECK(ptr.use_count() == 1U);
    CHECK(s.construct == 1U);
    CHECK(s.destroy == 1U);
  }
  CHECK(ptr.use_count() == 1U);
}

} // unnamed namespace
