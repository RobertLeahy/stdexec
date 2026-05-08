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
#include <variant>

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

  TEST_CASE("Variant trampolining works",
            "[detail][trampoline]")
  {
    bool a_invoked = false;
    auto a = [&]() noexcept {
      CHECK(!a_invoked);
      a_invoked = true;
    };
    bool b_invoked = false;
    auto b = [&]() noexcept {
      CHECK(!b_invoked);
      b_invoked = true;
    };
    std::size_t invoked = 0;
    const auto f = [&]() noexcept {
      using type = std::variant<
        decltype(a),
        decltype(b)>;
      if (invoked) {
        ++invoked;
        return type(b);
      }
      ++invoked;
      return type(a);
    };
    __trampoline(f);
    CHECK(invoked == 1);
    CHECK(a_invoked);
    CHECK(!b_invoked);
    __trampoline(f);
    CHECK(invoked == 2);
    CHECK(a_invoked);
    CHECK(b_invoked);
  }

  TEST_CASE("Optional can be directly trampolined",
            "[detail][trampoline]")
  {
    bool invoked = false;
    auto f = [&]() noexcept {
      CHECK(!invoked);
      invoked = true;
    };
    std::optional<decltype(f)> o(f);
    __trampoline(o);
    CHECK(invoked);
    o.reset();
    __trampoline(o);
  }

  TEST_CASE("Variant can be directly trampolined",
            "[detail][trampoline]")
  {
    bool a_invoked = false;
    auto a = [&]() noexcept {
      CHECK(!a_invoked);
      a_invoked = true;
    };
    bool b_invoked = false;
    auto b = [&]() noexcept {
      CHECK(!b_invoked);
      b_invoked = true;
    };
    std::variant<decltype(a), decltype(b)> v(a);
    __trampoline(v);
    CHECK(a_invoked);
    CHECK(!b_invoked);
    v.emplace<decltype(b)>(b);
    __trampoline(v);
    CHECK(a_invoked);
    CHECK(b_invoked);
  }

}  // namespace
