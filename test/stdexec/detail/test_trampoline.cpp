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

#include <optional>
#include <type_traits>

namespace
{
  struct terminal
  {
    int* log_;

    void operator()() && noexcept
    {
      *log_ = *log_ * 10 + 4;
    }
  };

  struct second
  {
    int* log_;

    terminal operator()() && noexcept
    {
      *log_ = *log_ * 10 + 3;
      return terminal{log_};
    }
  };

  struct first
  {
    int* log_;

    second operator()() && noexcept
    {
      *log_ = *log_ * 10 + 2;
      return second{log_};
    }
  };

  struct make_terminal
  {
    terminal operator()(int* log) && noexcept
    {
      *log = *log * 10 + 1;
      return terminal{log};
    }
  };

  struct make_first
  {
    first operator()(int* log) && noexcept
    {
      *log = *log * 10 + 1;
      return first{log};
    }
  };

  struct make_void
  {
    void operator()(int* log) && noexcept
    {
      *log = *log * 10 + 1;
    }
  };

  struct throwing_factory
  {
    terminal operator()(int*) &&;
  };

  struct cycle
  {
    cycle operator()() && noexcept;
  };

  struct make_cycle
  {
    cycle operator()(int*) && noexcept;
  };

  struct loop_step
  {
    int* remaining_;
    int* count_;

    std::optional<loop_step> operator()() && noexcept
    {
      ++*count_;
      if (--*remaining_ == 0)
      {
        return std::nullopt;
      }
      return loop_step{remaining_, count_};
    }
  };

  struct make_loop
  {
    loop_step operator()(int* remaining, int* count) && noexcept
    {
      return loop_step{remaining, count};
    }
  };

  struct observes_first_destroyed
  {
    bool* first_destroyed_;
    bool* observed_;

    void operator()() && noexcept
    {
      *observed_ = *first_destroyed_;
    }
  };

  struct first_with_observed_lifetime
  {
    bool* first_destroyed_;
    bool* observed_;

    first_with_observed_lifetime(bool* first_destroyed, bool* observed) noexcept
      : first_destroyed_(first_destroyed)
      , observed_(observed)
    {}

    first_with_observed_lifetime(first_with_observed_lifetime&& __other) noexcept
      : first_destroyed_(__other.first_destroyed_)
      , observed_(__other.observed_)
    {}

    ~first_with_observed_lifetime()
    {
      *first_destroyed_ = true;
    }

    observes_first_destroyed operator()() && noexcept
    {
      return observes_first_destroyed{first_destroyed_, observed_};
    }
  };

  struct make_first_with_observed_lifetime
  {
    first_with_observed_lifetime operator()(bool* first_destroyed, bool* observed) && noexcept
    {
      return first_with_observed_lifetime{first_destroyed, observed};
    }
  };

  template <class _Fun>
  concept can_call_trampoline = requires(_Fun __fun, int* __log) {
    STDEXEC::__trampoline(static_cast<_Fun&&>(__fun), __log);
  };

  template <class _Fun>
  concept can_make_deferred_trampoline = requires(_Fun __fun, int* __log) {
    STDEXEC::__deferred_trampoline{static_cast<_Fun&&>(__fun), __log};
  };

  using deferred_trampoline_t = STDEXEC::__deferred_trampoline<terminal>;

  static_assert(!std::is_copy_constructible_v<deferred_trampoline_t>);
  static_assert(!std::is_copy_assignable_v<deferred_trampoline_t>);
  static_assert(!std::is_move_constructible_v<deferred_trampoline_t>);
  static_assert(!std::is_move_assignable_v<deferred_trampoline_t>);
  static_assert(can_call_trampoline<make_terminal>);
  static_assert(can_call_trampoline<make_void>);
  static_assert(!can_call_trampoline<throwing_factory>);
  static_assert(can_make_deferred_trampoline<make_terminal>);
  static_assert(!can_make_deferred_trampoline<throwing_factory>);
  static_assert(STDEXEC::__trampoline_unit<cycle>);
  static_assert(can_call_trampoline<make_cycle>);
  static_assert(can_make_deferred_trampoline<make_cycle>);
  static_assert(STDEXEC::__trampoline_unit<std::optional<loop_step>>);

  TEST_CASE("trampoline invokes a void-returning callable immediately", "[detail][trampoline]")
  {
    int log = 0;

    STDEXEC::__trampoline(make_void{}, &log);

    CHECK(log == 1);
  }

  TEST_CASE("trampoline invokes a void-returning trampoline unit immediately",
            "[detail][trampoline]")
  {
    int log = 0;

    STDEXEC::__trampoline(make_terminal{}, &log);

    CHECK(log == 14);
  }

  TEST_CASE("trampoline iterates through returned invocables immediately", "[detail][trampoline]")
  {
    int log = 0;

    STDEXEC::__trampoline(make_first{}, &log);

    CHECK(log == 1234);
  }

  TEST_CASE("trampoline iterates through optional-like returned invocables",
            "[detail][trampoline]")
  {
    int remaining = 5;
    int count     = 0;

    STDEXEC::__trampoline(make_loop{}, &remaining, &count);

    CHECK(count == 5);
  }

  TEST_CASE("trampoline destroys the initial unit before invoking the next unit",
            "[detail][trampoline]")
  {
    bool first_destroyed = false;
    bool observed        = false;

    STDEXEC::__trampoline(make_first_with_observed_lifetime{}, &first_destroyed, &observed);

    CHECK(first_destroyed);
    CHECK(observed);
  }

  TEST_CASE("deferred trampoline invokes a void-returning callable on destruction",
            "[detail][trampoline]")
  {
    int log = 0;

    {
      STDEXEC::__deferred_trampoline trampoline{make_terminal{}, &log};
      CHECK(log == 1);
    }

    CHECK(log == 14);
  }

  TEST_CASE("deferred trampoline iterates through returned invocables on destruction",
            "[detail][trampoline]")
  {
    int log = 0;

    {
      STDEXEC::__deferred_trampoline trampoline{make_first{}, &log};
      CHECK(log == 1);
    }

    CHECK(log == 1234);
  }
}  // namespace
