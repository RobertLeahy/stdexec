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
#include <exec/is_nothrow_connectable.hpp>
#include <exec/static_thread_pool.hpp>
#include <exec/timed_scheduler.hpp>
#include <exec/timed_thread_scheduler.hpp>
#include <exec/variant_sender.hpp>
#include <stdexec/execution.hpp>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <exception>
#include <limits>
#include <memory>
#include <new>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

#include "../test_common/receivers.hpp"
#include "../test_common/type_helpers.hpp"

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
    auto construct(void* storage) noexcept {
      return
        ::stdexec::just(ptr) |
        ::stdexec::then([this, storage](auto&&) noexcept {
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
  static_assert(detail::with::constructor_in<
    decltype(std::declval<object&>().construct(nullptr)),
    ::stdexec::env<>>);
  static_assert(!detail::with::constructor_in<
    decltype(::stdexec::just(5)),
    ::stdexec::env<>>);
  static_assert(detail::with::destructor_in<
    decltype(std::declval<object>().destroy()),
    ::stdexec::env<>>);
  static_assert(!detail::with::destructor_in<
    decltype(::stdexec::just(5)),
    ::stdexec::env<>>);
  static_assert(!detail::with::destructor_in<
    decltype(::stdexec::just_stopped()),
    ::stdexec::env<>>);
  static_assert(!detail::with::destructor_in<
    decltype(::stdexec::just_error(5)),
    ::stdexec::env<>>);
  static_assert(::exec::object_in<object, ::stdexec::env<>>);
  struct tag {};
  class derived : public detail::with::object_state<
    derived,
    ::stdexec::env<>,
    tag,
    object>
  {
    using base_ = detail::with::object_state<
      derived,
      ::stdexec::env<>,
      tag,
      object>;
  public:
    using base_::base_;
    void constructed(const tag&) noexcept {
      ++constructed_count;
    }
    void destroyed(const tag&) noexcept {
      ++destroyed_count;
    }
    auto get_env() const noexcept {
      return ::stdexec::env<>{};
    }
    std::size_t constructed_count{0};
    std::size_t destroyed_count{0};
  };
  state s;
  {
    derived d(object{s, ptr});
    CHECK(ptr.use_count() == 1U);
    CHECK(s.construct == 0U);
    CHECK(s.destroy == 0U);
    CHECK(d.constructed_count == 0U);
    CHECK(d.destroyed_count == 0U);
    d.connect_construct();
    CHECK(ptr.use_count() == 2U);
    CHECK(s.construct == 0U);
    CHECK(s.destroy == 0U);
    CHECK(d.constructed_count == 0U);
    CHECK(d.destroyed_count == 0U);
    d.start_construct();
    CHECK(ptr.use_count() == 1U);
    CHECK(s.construct == 1U);
    CHECK(s.destroy == 0U);
    CHECK(d.constructed_count == 1U);
    CHECK(d.destroyed_count == 0U);
    d.connect_destroy();
    CHECK(ptr.use_count() == 2U);
    CHECK(s.construct == 1U);
    CHECK(s.destroy == 0U);
    CHECK(d.constructed_count == 1U);
    CHECK(d.destroyed_count == 0U);
    d.start_destroy();
    CHECK(ptr.use_count() == 1U);
    CHECK(s.construct == 1U);
    CHECK(s.destroy == 1U);
    CHECK(d.constructed_count == 1U);
    CHECK(d.destroyed_count == 1U);
  }
  CHECK(ptr.use_count() == 1U);
}

//  This is just here to make sure we'll get a compiler error if we ever try to
//  move the objects we're async constructing and destroying
template<typename T>
struct immovable_wrapper {
  explicit immovable_wrapper(T t) noexcept(
    std::is_nothrow_move_constructible_v<T>)
    : obj(std::move(t))
  {}
  immovable_wrapper(const immovable_wrapper&) = delete;
  immovable_wrapper& operator=(const immovable_wrapper&) = delete;
  T obj;
};

TEST_CASE("Multiple async objects work", "[with]") {
  struct unary_object {
    using type = immovable_wrapper<int>;
    auto construct(void* const storage) noexcept {
      return
        ::stdexec::just() |
        ::stdexec::then([storage]() noexcept {
          new(storage) immovable_wrapper<int>(5);
        });
    }
    auto destroy(type* const storage) && noexcept {
      return
        ::stdexec::just() |
        ::stdexec::then([storage]() noexcept {
          //  TODO?
          (void)storage;
        });
    }
  };
  static_assert(object_in<unary_object, ::stdexec::env<>>);
  struct nullary_object {
    using type = unsigned;
    auto construct() noexcept {
      return
        ::stdexec::read_env(get_uninitialized_storage) |
        ::stdexec::then([](void* const storage) noexcept {
          new(storage) unsigned(5);
        });
    }
    auto destroy() && noexcept {
      return
        ::stdexec::read_env(get_initialized_storage) |
        ::stdexec::then([](type* const storage) noexcept {
          (void)storage;
        });
    }
  };
  static_assert(object_in<nullary_object, ::stdexec::env<>>);
  static_assert(
    std::is_same_v<
      detail::with::objects_completion_signatures<
        ::stdexec::env<>,
        unary_object,
        nullary_object>::type,
      ::stdexec::completion_signatures<>>);
  const auto read_env = [](auto query) noexcept {
    return ::stdexec::read_env(query) | ::stdexec::then([](auto& ref) noexcept {
      return std::addressof(ref);
    });
  };
  bool invoked = false;
  auto sender = read_env(get_object<0>) | ::stdexec::let_value(
    [&](immovable_wrapper<int>* const a) {
      return read_env(get_object<1>) | ::stdexec::then(
        [&, a](unsigned* const b) {
          CHECK(!invoked);
          invoked = true;
          REQUIRE(a);
          CHECK(a->obj == 5);
          REQUIRE(b);
          CHECK(*b == 5);
        });
    });
  static_assert(
    set_equivalent<
      detail::with::main_completion_signatures<
        decltype(sender),
        ::stdexec::env<>,
        unary_object,
        nullary_object>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(),
        //  Because the lambdas aren't noexcept
        ::stdexec::set_error_t(std::exception_ptr)>>);
  static_assert(
    set_equivalent<
      detail::with::completion_signatures<
        decltype(sender),
        ::stdexec::env<>,
        unary_object,
        nullary_object>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(),
        ::stdexec::set_error_t(std::exception_ptr)>>);
  auto op = ::stdexec::connect(
    with(std::move(sender), unary_object{}, nullary_object{}),
    expect_void_receiver{});
  CHECK(!invoked);
  ::stdexec::start(op);
  CHECK(invoked);
}

TEST_CASE("Void async object works", "[with]") {
  struct object {
    auto construct() noexcept {
      return ::stdexec::just();
    }
    auto destroy() && noexcept {
      return ::stdexec::just();
    }
  };
  static_assert(object_in<object, ::stdexec::env<>>);
  std::size_t invoked = 0;
  auto sender = ::stdexec::just() | ::stdexec::then([&]() noexcept {
    ++invoked;
  });
  static_assert(
    set_equivalent<
      detail::with::main_completion_signatures<
        decltype(sender),
        ::stdexec::env<>,
        object>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t()>>);
  static_assert(
    set_equivalent<
      detail::with::completion_signatures<
        decltype(sender),
        ::stdexec::env<>,
        object>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t()>>);
  auto op = ::stdexec::connect(
    with(std::move(sender), object{}),
    expect_void_receiver{});
  CHECK(invoked == 0);
  ::stdexec::start(op);
  CHECK(invoked == 1);
}

TEST_CASE("Throwing constructors work", "[with]") {
  struct object {
    auto construct() noexcept {
      auto fail = ::stdexec::just_error(
        std::make_exception_ptr(
          std::runtime_error("Throwing as requested")));
      auto succeed = ::stdexec::just();
      using return_type = ::exec::variant_sender<
        decltype(fail),
        decltype(succeed)>;
      if (this->fail) {
        return return_type(std::move(fail));
      }
      return return_type(std::move(succeed));
    }
    auto destroy() && noexcept {
      return ::stdexec::just();
    }
    bool fail{false};
  };
  static_assert(object_in<object, ::stdexec::env<>>);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures<
        ::stdexec::set_error_t(std::exception_ptr)>,
      detail::with::objects_completion_signatures<
        ::stdexec::env<>,
        object>::type>);
  static_assert(
    noexcept(
      detail::with::get_constructor(
        std::declval<object&>(),
        std::declval<detail::with::storage_for<object>&>())));
  std::size_t invoked = 0;
  const auto sender = ::stdexec::just() | ::stdexec::then([&]() noexcept {
    ++invoked;
  });
  {
    auto op = ::stdexec::connect(
      with(
        sender,
        []() noexcept {
          object retr;
          retr.fail = true;
          return retr;
        }()),
      expect_error_receiver{});
    ::stdexec::start(op);
    CHECK(invoked == 0);
  }
  invoked = 0;
  {
    auto with_sender = with(
      sender,
      object{});
    static_assert(
      set_equivalent<
        ::stdexec::completion_signatures_of_t<
          decltype(with_sender),
          ::stdexec::env<>>,
        ::stdexec::completion_signatures<
          ::stdexec::set_value_t(),
          ::stdexec::set_error_t(std::exception_ptr)>>);
    auto op = ::stdexec::connect(
      std::move(with_sender),
      expect_void_receiver{});
    CHECK(invoked == 0);
    ::stdexec::start(op);
    CHECK(invoked == 1);
  }
}

TEST_CASE("Objects whose constructor factory throws work", "[with]") {
  struct object {
    auto construct() {
      if (fail) {
        throw std::runtime_error("Throwing as requested");
      }
      return ::stdexec::just();
    }
    auto destroy() && noexcept {
      return ::stdexec::just();
    }
    bool fail{false};
  };
  static_assert(object_in<object, ::stdexec::env<>>);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures<>,
      detail::with::objects_completion_signatures<
        ::stdexec::env<>,
        object>::type>);
  static_assert(
    !noexcept(
      detail::with::get_constructor(
        std::declval<object&>(),
        std::declval<detail::with::storage_for<object>&>())));
  static_assert(
    !std::is_nothrow_constructible_v<
      detail::with::operation_state<
        decltype(::stdexec::just()),
        expect_void_receiver<>,
        object>,
      decltype(::stdexec::just()),
      expect_void_receiver<>,
      object>);
  std::size_t invoked = 0;
  const auto sender = ::stdexec::just() | ::stdexec::then([&]() noexcept {
    ++invoked;
  });
  {
    auto with_sender = with(
      sender,
      object{});
    static_assert(
      set_equivalent<
        ::stdexec::completion_signatures_of_t<
          decltype(with_sender),
          ::stdexec::env<>>,
        ::stdexec::completion_signatures<
          ::stdexec::set_value_t()>>);
    auto op = ::stdexec::connect(
      std::move(with_sender),
      expect_void_receiver{});
    CHECK(invoked == 0);
    ::stdexec::start(op);
    CHECK(invoked == 1);
  }
  invoked = 0;
  CHECK_THROWS(::stdexec::connect(
    with(
      sender,
      []() noexcept {
        object retr;
        retr.fail = true;
        return retr;
      }()),
    make_fun_receiver([&]() noexcept { ++invoked; })));
  CHECK(invoked == 0);
}

//struct maybe_throws_on_move {
//  maybe_throws_on_move() = default;
//  maybe_throws_on_move(maybe_throws_on_move&& other) {
//    if (other.throws) {
//      throw std::runtime_error("Throwing as requested");
//    }
//  }
//  bool throws{false};
//};

struct throws_on_move_after {
  explicit throws_on_move_after(std::size_t& after) noexcept
    : after(after)
  {}
  throws_on_move_after(throws_on_move_after&& other)
    : after(other.after)
  {
    if (after) {
      --other.after;
    } else {
      throw std::runtime_error("Throwing as requested");
    }
  }
  throws_on_move_after& operator=(throws_on_move_after&&) = delete;
private:
  std::size_t& after;
};

TEST_CASE("Constructors whose connect throws work", "[with]") {
  struct object {
    auto construct() noexcept {
      static_assert(
        is_nothrow_connectable_v<
          decltype(::stdexec::just()),
          ::stdexec::env<>>);
      static_assert(
        is_nothrow_connectable_v<
          decltype(::stdexec::just(5)),
          ::stdexec::env<>>);
      static_assert(
        !is_nothrow_connectable_v<
          decltype(::stdexec::just(throws_on_move_after(after))),
          ::stdexec::env<>>);
      const auto impl = [&]() {
        return
          ::stdexec::just(throws_on_move_after(after)) |
          //  This just gets us a nullary set_value, which is required for an
          //  async constructor
          ::stdexec::then([](auto&&) noexcept {});
      };
      static_assert(
        !is_nothrow_connectable_v<
          decltype(impl()),
          ::stdexec::env<>>);
      static_assert(
        std::is_same_v<
          ::stdexec::completion_signatures<
            ::stdexec::set_value_t()>,
          ::stdexec::completion_signatures_of_t<
            decltype(impl()),
            ::stdexec::env<>>>);
      auto orig = after;
      for (;;) {
        after = orig;
        try {
          return impl();
        } catch (...) {
          ++orig;
        }
      }
    }
    auto destroy() && noexcept {
      return ::stdexec::just();
    }
    std::size_t& after;
  };
  static_assert(object_in<object, ::stdexec::env<>>);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures<>,
      detail::with::constructor_completion_signatures<
        decltype(
          std::declval<object&>().construct()),
        ::stdexec::env<>>>);
  static_assert(
    noexcept(
      detail::with::get_constructor(
        std::declval<object&>(),
        std::declval<detail::with::storage_for<object>&>())));
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures<>,
      detail::with::objects_completion_signatures<
        ::stdexec::env<>,
        object>::type>);
  std::size_t after = 0;
  bool threw = false;
  std::size_t completed = 0;
  std::size_t invoked = 0;
  auto op = [&]() noexcept {
    for (;; ++after) {
      const auto orig = after;
      try {
        auto sender = with(
          ::stdexec::just() | ::stdexec::then([&]() noexcept {
            ++completed;
          }),
          object{after});
        static_assert(
          set_equivalent<
            ::stdexec::completion_signatures_of_t<
              decltype(sender),
              ::stdexec::env<>>,
            ::stdexec::completion_signatures<
              ::stdexec::set_value_t()>>);
        //  This is the line that will throw because we connect the constructors
        //  in the connect of the with sender
        return ::stdexec::connect(
          std::move(sender),
          make_fun_receiver([&]() noexcept {
            ++invoked;
          }));
      } catch (...) {
        threw = true;
      }
      after = orig;
    }
  }();
  CHECK(after == 0);
  CHECK(threw);
  CHECK(completed == 0);
  CHECK(invoked == 0);
  ::stdexec::start(op);
  CHECK(completed == 1);
  CHECK(invoked == 1);
}

TEST_CASE("If connecting the main sender fails after async construction the "
  "exception is propagated and destruction proceeds", "[with]")
{
  struct object {
    auto construct() noexcept {
      return ::stdexec::just();
    }
    auto destroy() && noexcept {
      return
        ::stdexec::just() |
        ::stdexec::then([destroyed = &destroyed]() noexcept {
          ++*destroyed;
        });
    }
    std::size_t& destroyed;
  };
  std::size_t destroyed = 0;
  std::size_t after = 0;
  std::size_t invoked = 0;
  auto op = [&]() noexcept {
    for (;; ++after) {
      const auto orig = after;
      try {
        auto sender =
          ::stdexec::just(throws_on_move_after(after)) |
          ::stdexec::then([](auto&&) noexcept {});
        static_assert(
          set_equivalent<
            ::stdexec::completion_signatures_of_t<
              decltype(sender),
              ::stdexec::env<>>,
            ::stdexec::completion_signatures<
              ::stdexec::set_value_t()>>);
        static_assert(
          set_equivalent<
            detail::with::main_completion_signatures<
              decltype(sender),
              ::stdexec::env<>,
              object>,
            ::stdexec::completion_signatures<
              ::stdexec::set_value_t(),
              ::stdexec::set_error_t(std::exception_ptr)>>);
        auto with_sender = with(std::move(sender), object{destroyed});
        static_assert(
          set_equivalent<
            ::stdexec::completion_signatures_of_t<
              decltype(with_sender),
              ::stdexec::env<>>,
            ::stdexec::completion_signatures<
              ::stdexec::set_value_t(),
              ::stdexec::set_error_t(std::exception_ptr)>>);
        return ::stdexec::connect(
          std::move(with_sender) |
            ::stdexec::upon_error([&](std::exception_ptr&&) noexcept {
              ++invoked;
            }),
          make_fun_receiver([]() noexcept {}));
      } catch (...) {}
      after = orig;
    }
  }();
  //  Ensure that the next time the object is moved it'll throw, thereby causing
  //  connect to throw
  CHECK(destroyed == 0);
  CHECK(after == 0);
  CHECK(invoked == 0);
  ::stdexec::start(op);
  CHECK(destroyed == 1);
  CHECK(invoked == 1);
}

TEST_CASE("When the main operation fails destructors are still run", "[with]") {
  struct object {
    auto construct() noexcept {
      return
        ::stdexec::just() |
        ::stdexec::then([constructed = &constructed]() noexcept {
          ++*constructed;
        });
    }
    auto destroy() && noexcept {
      return
        ::stdexec::just() |
        ::stdexec::then([destroyed = &destroyed]() noexcept {
          ++*destroyed;
        });
    }
    std::size_t& constructed;
    std::size_t& destroyed;
  };
  std::size_t constructed = 0;
  std::size_t destroyed = 0;
  auto sender = with(
    ::stdexec::just_error(
      std::make_exception_ptr(
        std::runtime_error("Failed"))),
    object{constructed, destroyed});
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures_of_t<
        decltype(sender),
        ::stdexec::env<>>,
      ::stdexec::completion_signatures<
        ::stdexec::set_error_t(std::exception_ptr)>>);
  auto op = ::stdexec::connect(
    with(
      ::stdexec::just_error(
        std::make_exception_ptr(
          std::runtime_error("Failed"))),
      object{constructed, destroyed}),
      expect_error_receiver{});
  CHECK(constructed == 0);
  CHECK(destroyed == 0);
  ::stdexec::start(op);
  CHECK(constructed == 1);
  CHECK(destroyed == 1);
}

TEST_CASE("An abandoned operation state cleans up the operation states of the "
  "constructors", "[with]")
{
  const auto ptr = std::make_shared<int>(5);
  struct object {
    auto construct() noexcept {
      return
        ::stdexec::just(ptr) |
        ::stdexec::then([](auto&&) noexcept {});
    }
    auto destroy() && noexcept {
      return ::stdexec::just();
    }
    const std::shared_ptr<int>& ptr;
  };
  CHECK(ptr.use_count() == 1);
  (void)::stdexec::connect(
    with(
      ::stdexec::just(),
      object{ptr}),
    make_fun_receiver([]() noexcept {}));
  CHECK(ptr.use_count() == 1);
}

TEST_CASE("The constructor's operation state is cleaned up after the "
  "constructor completes", "[with]")
{
  const auto ptr = std::make_shared<int>(5);
  struct object {
    auto construct() noexcept {
      return
        ::stdexec::just(ptr) |
        ::stdexec::then([](const auto& ptr) {
          CHECK(ptr.use_count() == 2);
        });
    }
    auto destroy() && noexcept {
      return ::stdexec::just();
    }
    const std::shared_ptr<int>& ptr;
  };
  CHECK(ptr.use_count() == 1);
  auto op = ::stdexec::connect(
    with(
      ::stdexec::just() | ::stdexec::then([&]() {
        CHECK(ptr.use_count() == 1);
      }),
      object{ptr}),
    make_fun_receiver([]() noexcept {}));
  CHECK(ptr.use_count() == 2);
  ::stdexec::start(op);
  CHECK(ptr.use_count() == 1);
}

TEST_CASE("A single failing constructor triggers fan in, prevents the "
  "initiation of the main operation, and runs all needful destructors",
  "[with]")
{
  struct state {
    explicit state(const std::size_t fail_after)
      : pool(5),
        fail_after(fail_after)
    {}
    static_thread_pool pool;
    std::atomic<std::size_t> fail_after;
    //  Starts at one so zero means an operation didn't happen
    std::atomic<std::size_t> current_op{1};
    std::array<std::size_t, 5> constructed{};
    std::array<std::size_t, 5> destroyed{};
    std::optional<std::size_t> who;
  };
  struct object {
    auto construct() noexcept {
      return
        ::stdexec::schedule(s.pool.get_scheduler()) |
        //  We don't want to be able to stop
        ::stdexec::upon_stopped([]() noexcept {}) |
        ::stdexec::then([&]() {
          CHECK(s.constructed[i] == 0);
          s.constructed[i] =
            s.current_op.fetch_add(1, std::memory_order_relaxed);
          if (!s.fail_after.fetch_sub(1, std::memory_order_relaxed)) {
            CHECK(!s.who);
            s.who = i;
            throw std::runtime_error("Throwing as requested");
          }
        });
    }
    auto destroy() noexcept {
      return
        ::stdexec::just() |
        ::stdexec::then([&]() noexcept {
          s.destroyed[i] =
            s.current_op.fetch_add(1, std::memory_order_relaxed);
        });
    }
    state& s;
    std::size_t i;
  };
  static_assert(object_in<object, ::stdexec::env<>>);
  std::size_t fail_after = 0;
  for (std::size_t fail_after = 0; fail_after < 5; ++fail_after) {
    state s(fail_after);
    auto sender = with(
      ::stdexec::just() | ::stdexec::then([&]() {
        FAIL("Main operation should not be run due to constructor failure");
      }),
      object{s, 0},
      object{s, 1},
      object{s, 2},
      object{s, 3},
      object{s, 4});
    static_assert(
      set_equivalent<
        ::stdexec::completion_signatures_of_t<
          decltype(sender),
          ::stdexec::env<>>,
        ::stdexec::completion_signatures<
          ::stdexec::set_value_t(),
          ::stdexec::set_error_t(std::exception_ptr)>>);
    CHECK_THROWS(::stdexec::sync_wait(std::move(sender)));
    CHECK(s.who);
    for (std::size_t i = 0; i < 5; ++i) {
      CHECK(s.constructed[i] != 0);
      if (s.who == i) {
        CHECK(s.destroyed[i] == 0);
      } else {
        CHECK(s.destroyed[i] > s.constructed[i]);
      }
    }
  }
}

TEST_CASE("When a constructor fails other constructors are stopped to accelerate fan in", "[with]")
{
  struct fail_object {
    auto construct() noexcept {
      return
        ::stdexec::just() |
        ::stdexec::then([]() {
          throw std::runtime_error("Failing");
        });
    }
    auto destroy() noexcept {
      return ::stdexec::just();
    }
  };
  struct never_complete_object {
    auto construct() noexcept {
      return schedule_after(
        ctx.get_scheduler(),
        std::chrono::years(1));
    }
    auto destroy() noexcept {
      return ::stdexec::just();
    }
    timed_thread_context& ctx;
  };
  timed_thread_context ctx;
  const auto impl = [&](auto&&... objects) {
    auto sender = with(
      ::stdexec::just() |
      ::stdexec::then([]() {
        FAIL("Main operation should not have been started");
      }),
      std::forward<decltype(objects)>(objects)...);
    static_assert(
      set_equivalent<
        ::stdexec::completion_signatures_of_t<
          decltype(sender),
          ::stdexec::env<>>,
        ::stdexec::completion_signatures<
          ::stdexec::set_value_t(),
          ::stdexec::set_stopped_t(),
          ::stdexec::set_error_t(std::exception_ptr)>>);
    return sender;
  };
  CHECK_THROWS(
    ::stdexec::sync_wait(
      impl(fail_object{}, never_complete_object{ctx})));
  CHECK_THROWS(
    ::stdexec::sync_wait(
      impl(never_complete_object{ctx}, fail_object{})));
}

} // unnamed namespace
