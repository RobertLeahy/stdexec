/*
 * Copyright (c) 2026 NVIDIA Corporation
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

#include <stdexec/__detail/__trampoline.hpp>

#include <catch2/catch_all.hpp>

#include <cstddef>
#include <optional>
#include <type_traits>

namespace
{

  using namespace ::STDEXEC;

  static_assert(
    std::is_same_v<
      __tramp::__list<int>,
      __tramp::__add<
        __tramp::__list<>,
        int>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<int>,
      __tramp::__add<
        __tramp::__list<int>,
        int>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<int>,
      __tramp::__add<
        __tramp::__list<>,
        std::optional<int>>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<int>,
      __tramp::__add<
        __tramp::__list<int>,
        std::optional<int>>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<int>,
      __tramp::__add<
        __tramp::__list<int>,
        std::variant<int>>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<int>,
      __tramp::__add<
        __tramp::__list<>,
        std::variant<int>>::__t>);
  static_assert(
    __tramp::__equivalent<
      __tramp::__list<int, float>,
      __tramp::__add<
        __tramp::__list<>,
        std::variant<int, float>>::__t>::value);

  struct terminal {
    void operator()() && noexcept;
  };

  static_assert(__trampolinable<terminal>);

  static_assert(
    std::is_same_v<
      __tramp::__list<terminal>,
      __tramp::__add_callback<
        __tramp::__list<>,
        terminal>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<terminal>,
      __tramp::__add_callback<
        __tramp::__list<terminal>,
        terminal>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<terminal>,
      __tramp::__add_callback<
        __tramp::__list<>,
        std::optional<terminal>>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<terminal>,
      __tramp::__add_callback<
        __tramp::__list<terminal>,
        std::optional<terminal>>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<terminal>,
      __tramp::__add_callback<
        __tramp::__list<>,
        std::variant<terminal>>::__t>);
  static_assert(
    std::is_same_v<
      __tramp::__list<terminal>,
      __tramp::__add_callback<
        __tramp::__list<terminal>,
        std::variant<terminal>>::__t>);

  struct optionally_terminal {
    std::optional<terminal> operator()() && noexcept;
  };

  static_assert(__trampolinable<optionally_terminal>);

  static_assert(
    __tramp::__equivalent<
      __tramp::__list<terminal, optionally_terminal>,
      __tramp::__add_callback<
        __tramp::__list<>,
        optionally_terminal>::__t>::value);
  static_assert(
    std::is_same_v<
      __tramp::__list<terminal>,
      __tramp::__add_callback<
        __tramp::__list<terminal>,
        terminal>::__t>);

  //struct terminal
  //{
  //  int* log_;

  //  void operator()() && noexcept
  //  {
  //    *log_ = *log_ * 10 + 4;
  //  }
  //};

  //struct second
  //{
  //  int* log_;

  //  terminal operator()() && noexcept
  //  {
  //    *log_ = *log_ * 10 + 3;
  //    return terminal{log_};
  //  }
  //};

  //struct first
  //{
  //  int* log_;

  //  second operator()() && noexcept
  //  {
  //    *log_ = *log_ * 10 + 2;
  //    return second{log_};
  //  }
  //};

  //struct make_terminal
  //{
  //  terminal operator()(int* log) && noexcept
  //  {
  //    *log = *log * 10 + 1;
  //    return terminal{log};
  //  }
  //};

  //struct make_first
  //{
  //  first operator()(int* log) && noexcept
  //  {
  //    *log = *log * 10 + 1;
  //    return first{log};
  //  }
  //};

  //struct make_variant
  //{
  //  STDEXEC::__variant<first, terminal> operator()(int* log, bool first_path) && noexcept
  //  {
  //    STDEXEC::__variant<first, terminal> variant{STDEXEC::__no_init};
  //    if (first_path)
  //    {
  //      variant.template emplace<first>(log);
  //    }
  //    else
  //    {
  //      variant.template emplace<terminal>(log);
  //    }
  //    *log = *log * 10 + 1;
  //    return variant;
  //  }
  //};

  //struct make_nested_variant
  //{
  //  STDEXEC::__variant<STDEXEC::__variant<first, terminal>, second>
  //    operator()(int* log, int path) && noexcept
  //  {
  //    STDEXEC::__variant<STDEXEC::__variant<first, terminal>, second> outer{STDEXEC::__no_init};
  //    if (path == 0)
  //    {
  //      outer.template emplace<second>(log);
  //    }
  //    else
  //    {
  //      auto& inner =
  //        outer.template emplace<STDEXEC::__variant<first, terminal>>(STDEXEC::__no_init);
  //      if (path == 1)
  //      {
  //        inner.template emplace<first>(log);
  //      }
  //      else
  //      {
  //        inner.template emplace<terminal>(log);
  //      }
  //    }
  //    *log = *log * 10 + 1;
  //    return outer;
  //  }
  //};

  //struct make_void
  //{
  //  void operator()(int* log) && noexcept
  //  {
  //    *log = *log * 10 + 1;
  //  }
  //};

  //struct throwing_factory
  //{
  //  terminal operator()(int*) &&;
  //};

  //struct cycle
  //{
  //  cycle operator()() && noexcept;
  //};

  //struct make_cycle
  //{
  //  cycle operator()(int*) && noexcept;
  //};

  //struct loop_step
  //{
  //  int* remaining_;
  //  int* count_;

  //  std::optional<loop_step> operator()() && noexcept
  //  {
  //    ++*count_;
  //    if (--*remaining_ == 0)
  //    {
  //      return std::nullopt;
  //    }
  //    return loop_step{remaining_, count_};
  //  }
  //};

  //struct make_loop
  //{
  //  loop_step operator()(int* remaining, int* count) && noexcept
  //  {
  //    return loop_step{remaining, count};
  //  }
  //};

  //struct observes_first_destroyed
  //{
  //  bool* first_destroyed_;
  //  bool* observed_;

  //  void operator()() && noexcept
  //  {
  //    *observed_ = *first_destroyed_;
  //  }
  //};

  //struct first_with_observed_lifetime
  //{
  //  bool* first_destroyed_;
  //  bool* observed_;

  //  first_with_observed_lifetime(bool* first_destroyed, bool* observed) noexcept
  //    : first_destroyed_(first_destroyed)
  //    , observed_(observed)
  //  {}

  //  first_with_observed_lifetime(first_with_observed_lifetime&& __other) noexcept
  //    : first_destroyed_(__other.first_destroyed_)
  //    , observed_(__other.observed_)
  //  {}

  //  ~first_with_observed_lifetime()
  //  {
  //    *first_destroyed_ = true;
  //  }

  //  observes_first_destroyed operator()() && noexcept
  //  {
  //    return observes_first_destroyed{first_destroyed_, observed_};
  //  }
  //};

  //struct make_first_with_observed_lifetime
  //{
  //  first_with_observed_lifetime operator()(bool* first_destroyed, bool* observed) && noexcept
  //  {
  //    return first_with_observed_lifetime{first_destroyed, observed};
  //  }
  //};

  //template <class _Fun>
  //concept can_call_trampoline = requires(_Fun __fun, int* __log) {
  //  STDEXEC::__trampoline(static_cast<_Fun&&>(__fun), __log);
  //};

  //template <class _Fun, class... _As>
  //concept can_call_trampoline_with = requires(_Fun __fun, _As... __as) {
  //  STDEXEC::__trampoline(static_cast<_Fun&&>(__fun), static_cast<_As&&>(__as)...);
  //};

  //template <class _Fun>
  //concept can_make_deferred_trampoline = requires(_Fun __fun, int* __log) {
  //  STDEXEC::__deferred_trampoline{static_cast<_Fun&&>(__fun), __log};
  //};

  //using deferred_trampoline_t = STDEXEC::__deferred_trampoline<terminal>;

  //static_assert(!std::is_copy_constructible_v<deferred_trampoline_t>);
  //static_assert(!std::is_copy_assignable_v<deferred_trampoline_t>);
  //static_assert(!std::is_move_constructible_v<deferred_trampoline_t>);
  //static_assert(!std::is_move_assignable_v<deferred_trampoline_t>);
  //static_assert(can_call_trampoline<make_terminal>);
  //static_assert(can_call_trampoline<make_void>);
  //static_assert(can_call_trampoline_with<make_variant, int*, bool>);
  //static_assert(can_call_trampoline_with<make_nested_variant, int*, int>);
  //static_assert(!can_call_trampoline<throwing_factory>);
  //static_assert(can_make_deferred_trampoline<make_terminal>);
  //static_assert(!can_make_deferred_trampoline<throwing_factory>);
  //static_assert(STDEXEC::__trampoline_unit<cycle>);
  //static_assert(STDEXEC::__trampoline_unit<STDEXEC::__variant<first, terminal>>);
  //static_assert(!STDEXEC::__trampoline_unit<STDEXEC::__variant<first, throwing_factory>>);
  //static_assert(can_call_trampoline<make_cycle>);
  //static_assert(can_make_deferred_trampoline<make_cycle>);
  //static_assert(STDEXEC::__trampoline_unit<std::optional<loop_step>>);

  //TEST_CASE("trampoline invokes a void-returning callable immediately", "[detail][trampoline]")
  //{
  //  int log = 0;

  //  STDEXEC::__trampoline(make_void{}, &log);

  //  CHECK(log == 1);
  //}

  //TEST_CASE("trampoline invokes a void-returning trampoline unit immediately",
  //          "[detail][trampoline]")
  //{
  //  int log = 0;

  //  STDEXEC::__trampoline(make_terminal{}, &log);

  //  CHECK(log == 14);
  //}

  //TEST_CASE("trampoline iterates through returned invocables immediately", "[detail][trampoline]")
  //{
  //  int log = 0;

  //  STDEXEC::__trampoline(make_first{}, &log);

  //  CHECK(log == 1234);
  //}

  //TEST_CASE("trampoline iterates through the active variant alternative",
  //          "[detail][trampoline]")
  //{
  //  int first_path_log = 0;
  //  int final_path_log = 0;

  //  STDEXEC::__trampoline(make_variant{}, &first_path_log, true);
  //  STDEXEC::__trampoline(make_variant{}, &final_path_log, false);

  //  CHECK(first_path_log == 1234);
  //  CHECK(final_path_log == 14);
  //}

  //TEST_CASE("trampoline flattens nested variant alternatives", "[detail][trampoline]")
  //{
  //  int second_path_log = 0;
  //  int first_path_log  = 0;
  //  int final_path_log  = 0;

  //  STDEXEC::__trampoline(make_nested_variant{}, &second_path_log, 0);
  //  STDEXEC::__trampoline(make_nested_variant{}, &first_path_log, 1);
  //  STDEXEC::__trampoline(make_nested_variant{}, &final_path_log, 2);

  //  CHECK(second_path_log == 134);
  //  CHECK(first_path_log == 1234);
  //  CHECK(final_path_log == 14);
  //}

  //TEST_CASE("trampoline iterates through optional-like returned invocables",
  //          "[detail][trampoline]")
  //{
  //  int remaining = 5;
  //  int count     = 0;

  //  STDEXEC::__trampoline(make_loop{}, &remaining, &count);

  //  CHECK(count == 5);
  //}

  //TEST_CASE("trampoline destroys the initial unit before invoking the next unit",
  //          "[detail][trampoline]")
  //{
  //  bool first_destroyed = false;
  //  bool observed        = false;

  //  STDEXEC::__trampoline(make_first_with_observed_lifetime{}, &first_destroyed, &observed);

  //  CHECK(first_destroyed);
  //  CHECK(observed);
  //}

  //TEST_CASE("deferred trampoline invokes a void-returning callable on destruction",
  //          "[detail][trampoline]")
  //{
  //  int log = 0;

  //  {
  //    STDEXEC::__deferred_trampoline trampoline{make_terminal{}, &log};
  //    CHECK(log == 1);
  //  }

  //  CHECK(log == 14);
  //}

  TEST_CASE("Invocable which returns void is invoked",
            "[detail][trampoline]")
  {
    bool invoked = false;
    __trampoline([&]() noexcept {
      CHECK(!invoked);
      invoked = true;
    });
    CHECK(invoked);
  }

  TEST_CASE("Invocable which returns disengaged optional is invoked",
            "[detail][trampoline]")
  {
    bool inner_invoked = false;
    auto f = [&]() noexcept {
      CHECK(!inner_invoked);
      inner_invoked = true;
    };
    bool invoked = false;
    __trampoline([&]() noexcept {
      CHECK(!invoked);
      invoked = true;
      return std::optional<decltype(f)>{};
    });
    CHECK(!inner_invoked);
    CHECK(invoked);
  }

  TEST_CASE("Invocable which loops using an optional is invoked along with all continuations",
            "[detail][trampoline]")
  {
    std::size_t inner_invoked = 0;
    bool invoked = false;
    struct type {
      std::size_t& inner_invoked;
      bool& invoked;
      std::optional<type> operator()() && noexcept {
        CHECK(invoked);
        ++inner_invoked;
        REQUIRE(inner_invoked < 4);
        if (inner_invoked == 3) {
          return {};
        }
        return *this;
      }
    };
    __trampoline([&]() noexcept {
      CHECK(!invoked);
      CHECK(!inner_invoked);
      invoked = true;
      return type{inner_invoked, invoked};
    });
    CHECK(inner_invoked == 3);
    CHECK(invoked);
  }
}  // namespace
