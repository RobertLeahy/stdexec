/*
 * Copyright (c) 2022 Lucian Radu Teodorescu
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

#include "test_common/receivers.hpp"
#include <catch2/catch_all.hpp>
#include <stdexec/execution.hpp>

#include <climits>
#include <string>

namespace ex = STDEXEC;

STDEXEC_PRAGMA_PUSH()
STDEXEC_PRAGMA_IGNORE_GNU("-Wunused-function")

namespace
{

  struct recv_value
  {
    int* target_;

    void set_value(int val) noexcept
    {
      *target_ = val;
    }

    void set_error(int ec) noexcept
    {
      *target_ = -ec;
    }

    void set_stopped() noexcept
    {
      *target_ = INT_MAX;
    }
  };

  struct recv_rvalref
  {
    int* target_;

    void set_value(int val) noexcept
    {
      *target_ = val;
    }

    void set_error(int ec) noexcept
    {
      *target_ = -ec;
    }

    void set_stopped() noexcept
    {
      *target_ = INT_MAX;
    }
  };

  struct recv_ref
  {
    int* target_;

    void set_value(int val) noexcept
    {
      *target_ = val;
    }

    void set_error(int ec) noexcept
    {
      *target_ = -ec;
    }

    void set_stopped() noexcept
    {
      *target_ = INT_MAX;
    }
  };

  struct recv_cref
  {
    int* target_;

    void set_value(int val) noexcept
    {
      *target_ = val;
    }

    void set_error(int ec) noexcept
    {
      *target_ = -ec;
    }

    void set_stopped() noexcept
    {
      *target_ = INT_MAX;
    }
  };

  struct continuation
  {
    int* target_;
    int  value_;

    void operator()() && noexcept
    {
      *target_ = value_;
    }
  };

  struct recv_deferred
  {
    int* target_;

    continuation set_value(int val) noexcept
    {
      *target_ = 1;
      return continuation{target_, val};
    }

    continuation set_error(int ec) noexcept
    {
      *target_ = 2;
      return continuation{target_, -ec};
    }

    continuation set_stopped() noexcept
    {
      *target_ = 3;
      return continuation{target_, INT_MAX};
    }
  };

  TEST_CASE("can call set_value on a void receiver", "[cpo][cpo_receiver]")
  {
    ex::set_value(expect_void_receiver{});
  }

  TEST_CASE("can call set_value on a int receiver", "[cpo][cpo_receiver]")
  {
    ex::set_value(expect_value_receiver{10}, 10);
  }

  TEST_CASE("can call set_value on a string receiver", "[cpo][cpo_receiver]")
  {
    ex::set_value(expect_value_receiver{std::string{"hello"}}, std::string{"hello"});
  }

  TEST_CASE("can call set_stopped on a receiver", "[cpo][cpo_receiver]")
  {
    ex::set_stopped(expect_stopped_receiver{});
  }

  TEST_CASE("can call set_error on a receiver", "[cpo][cpo_receiver]")
  {
    std::exception_ptr ex = std::make_exception_ptr(std::bad_alloc{});
    ex::set_error(expect_error_receiver{}, ex);
  }

  TEST_CASE("can call set_error with an error code on a receiver", "[cpo][cpo_receiver]")
  {
    std::error_code errCode{100, std::generic_category()};
    ex::set_error(expect_error_receiver{errCode}, errCode);
  }

  TEST_CASE("set_value with a value passes the value to the receiver", "[cpo][cpo_receiver]")
  {
    ex::set_value(expect_value_receiver{10}, 10);
  }

  TEST_CASE("can call set_value on a receiver with plain value type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_value_t, recv_value, int>,
                  "cannot call set_value on recv_value");
    int val = 0;
    ex::set_value(recv_value{&val}, 10);
    REQUIRE(val == 10);
  }

  TEST_CASE("can call set_value on a receiver with r-value ref type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_value_t, recv_rvalref, int>,
                  "cannot call set_value on recv_rvalref");
    int val = 0;
    ex::set_value(recv_rvalref{&val}, 10);
    REQUIRE(val == 10);
  }

  TEST_CASE("can call set_value on a receiver with ref type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_value_t, recv_ref&, int>,
                  "cannot call set_value on recv_ref");
    int      val = 0;
    recv_ref recv{&val};
    ex::set_value(recv, 10);
    REQUIRE(val == 10);
  }

  TEST_CASE("can call set_value on a receiver with const ref type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_value_t, recv_cref, int>,
                  "cannot call set_value on recv_cref");
    int val = 0;
    ex::set_value(recv_cref{&val}, 10);
    REQUIRE(val == 10);
  }

  TEST_CASE("can call set_error on a receiver with plain value type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_error_t, recv_value, int>,
                  "cannot call set_error on recv_value");
    int val = 0;
    ex::set_error(recv_value{&val}, 10);
    REQUIRE(val == -10);
  }

  TEST_CASE("can call set_error on a receiver with r-value ref type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_error_t, recv_rvalref, int>,
                  "cannot call set_error on recv_rvalref");
    int val = 0;
    ex::set_error(recv_rvalref{&val}, 10);
    REQUIRE(val == -10);
  }

  TEST_CASE("can call set_error on a receiver with ref type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_error_t, recv_ref&, int>,
                  "cannot call set_error on recv_ref");
    int      val = 0;
    recv_ref recv{&val};
    ex::set_error(recv, 10);
    REQUIRE(val == -10);
  }

  TEST_CASE("can call set_error on a receiver with const ref type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_error_t, recv_cref, int>,
                  "cannot call set_error on recv_cref");
    int val = 0;
    ex::set_error(recv_cref{&val}, 10);
    REQUIRE(val == -10);
  }

  TEST_CASE("can call set_stopped on a receiver with plain value type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_stopped_t, recv_value>,
                  "cannot call set_stopped on recv_value");
    int val = 0;
    ex::set_stopped(recv_value{&val});
    REQUIRE(val == INT_MAX);
  }

  TEST_CASE("can call set_stopped on a receiver with r-value ref type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_stopped_t, recv_rvalref>,
                  "cannot call set_stopped on recv_rvalref");
    int val = 0;
    ex::set_stopped(recv_rvalref{&val});
    REQUIRE(val == INT_MAX);
  }

  TEST_CASE("can call set_stopped on a receiver with ref type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_stopped_t, recv_ref&>,
                  "cannot call set_stopped on recv_ref");
    int      val = 0;
    recv_ref recv{&val};
    ex::set_stopped(recv);
    REQUIRE(val == INT_MAX);
  }

  TEST_CASE("can call set_stopped on a receiver with const ref type", "[cpo][cpo_receiver]")
  {
    static_assert(std::invocable<ex::set_stopped_t, recv_cref>,
                  "cannot call set_stopped on recv_cref");
    int val = 0;
    ex::set_stopped(recv_cref{&val});
    REQUIRE(val == INT_MAX);
  }

  TEST_CASE("set_value_or_defer returns a trampoline continuation", "[cpo][cpo_receiver]")
  {
    static_assert(std::is_same_v<ex::set_value_result_t<recv_value, int>, void>);
    static_assert(!ex::set_value_defers_v<recv_value, int>);
    static_assert(std::is_same_v<ex::set_value_result_t<recv_deferred, int>, continuation>);
    static_assert(ex::set_value_defers_v<recv_deferred, int>);

    int val = 0;

    auto next = ex::set_value_or_defer(recv_deferred{&val}, 10);

    CHECK(val == 1);
    ex::__trampoline(static_cast<continuation&&>(next));
    CHECK(val == 10);
  }

  TEST_CASE("set_error_or_defer returns a trampoline continuation", "[cpo][cpo_receiver]")
  {
    static_assert(std::is_same_v<ex::set_error_result_t<recv_value, int>, void>);
    static_assert(!ex::set_error_defers_v<recv_value, int>);
    static_assert(std::is_same_v<ex::set_error_result_t<recv_deferred, int>, continuation>);
    static_assert(ex::set_error_defers_v<recv_deferred, int>);

    int val = 0;

    auto next = ex::set_error_or_defer(recv_deferred{&val}, 10);

    CHECK(val == 2);
    ex::__trampoline(static_cast<continuation&&>(next));
    CHECK(val == -10);
  }

  TEST_CASE("set_stopped_or_defer returns a trampoline continuation", "[cpo][cpo_receiver]")
  {
    static_assert(std::is_same_v<ex::set_stopped_result_t<recv_value>, void>);
    static_assert(!ex::set_stopped_defers_v<recv_value>);
    static_assert(std::is_same_v<ex::set_stopped_result_t<recv_deferred>, continuation>);
    static_assert(ex::set_stopped_defers_v<recv_deferred>);

    int val = 0;

    auto next = ex::set_stopped_or_defer(recv_deferred{&val});

    CHECK(val == 3);
    ex::__trampoline(static_cast<continuation&&>(next));
    CHECK(val == INT_MAX);
  }

  TEST_CASE("receiver CPOs trampoline returned continuations internally", "[cpo][cpo_receiver]")
  {
    int value = 0;
    int error = 0;
    int stop  = 0;

    ex::set_value(recv_deferred{&value}, 10);
    ex::set_error(recv_deferred{&error}, 10);
    ex::set_stopped(recv_deferred{&stop});

    CHECK(value == 10);
    CHECK(error == -10);
    CHECK(stop == INT_MAX);
  }

  TEST_CASE("tag types can be deduced from set_value, set_error and set_stopped",
            "[cpo][cpo_receiver]")
  {
    static_assert(std::is_same_v<ex::set_value_t const, decltype(ex::set_value)>, "type mismatch");
    static_assert(std::is_same_v<ex::set_error_t const, decltype(ex::set_error)>, "type mismatch");
    static_assert(std::is_same_v<ex::set_stopped_t const, decltype(ex::set_stopped)>,
                  "type mismatch");
    static_assert(std::is_same_v<ex::set_value_or_defer_t const, decltype(ex::set_value_or_defer)>,
                  "type mismatch");
    static_assert(std::is_same_v<ex::set_error_or_defer_t const, decltype(ex::set_error_or_defer)>,
                  "type mismatch");
    static_assert(
      std::is_same_v<ex::set_stopped_or_defer_t const, decltype(ex::set_stopped_or_defer)>,
      "type mismatch");
  }
}  // namespace

STDEXEC_PRAGMA_POP()
