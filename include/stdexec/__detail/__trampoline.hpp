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
#pragma once

#include <algorithm>
#include <cstddef>
#include <memory>
#include <optional>
#include <type_traits>
#include <variant>

#include "__prologue.hpp"

namespace STDEXEC
{
  namespace __tramp
  {

    template<typename _T>
    concept __basic =
      std::is_invocable_v<_T> &&
      std::is_nothrow_invocable_v<_T> &&
      std::is_move_constructible_v<_T> &&
      std::is_nothrow_move_constructible_v<_T>;

    template<typename... _Ts>
    struct __list : std::type_identity<_Ts>... {
      static constexpr std::size_t __size() noexcept {
        if constexpr (sizeof...(_Ts)) {
          return std::max({sizeof(_Ts)...});
        } else {
          return 0;
        }
      }
      static constexpr std::size_t __align() noexcept {
        if constexpr (sizeof...(_Ts)) {
          return std::max({alignof(_Ts)...});
        } else {
          return 0;
        }
      }
      using __variant_t = std::variant<_Ts...>;
      template<typename _T>
      static constexpr bool __contains =
        std::is_base_of_v<
          std::type_identity<_T>,
          __list>;
    };

    template<typename, typename>
    struct __equivalent;
    template<typename... _Ts, typename... _Us>
    struct __equivalent<
      __list<_Ts...>,
      __list<_Us...>> : std::bool_constant<
        (sizeof...(_Ts) == sizeof...(_Us)) &&
        (__list<_Ts...>::template __contains<_Us> && ...)> {};

    template<typename, typename>
    struct __add;
    template<typename... _Ts, typename _T>
      requires __list<_Ts...>::template __contains<_T>
    struct __add<__list<_Ts...>, _T> {
      using __t = __list<_Ts...>;
    };
    template<typename... _Ts, typename _T>
    struct __add<__list<_Ts...>, _T> {
      using __t = __list<_Ts..., _T>;
    };
    template<typename... _Ts, typename _T>
    struct __add<__list<_Ts...>, std::optional<_T>> {
      using __t = __add<
        __list<_Ts...>,
        _T>::__t;
    };
    template<typename... _Ts>
    struct __add<__list<_Ts...>, __list<>> {
      using __t = __list<_Ts...>;
    };
    template<typename... _Ts, typename _U, typename... _Us>
    struct __add<__list<_Ts...>, __list<_U, _Us...>> {
      using __t = __add<
        typename __add<
          __list<_Ts...>,
          _U>::__t,
        __list<_Us...>>::__t;
    };
    template<typename... _Ts, typename... _Us>
    struct __add<__list<_Ts...>, std::variant<_Us...>> {
      using __t = __add<
        __list<_Ts...>,
        __list<_Us...>>::__t;
    };

    template<typename, typename>
    struct __add_callback;
    //  Callback already in list
    template<typename... _Ts, typename _T>
      requires
        //__basic<_T> &&
        __list<_Ts...>::template __contains<_T>
    struct __add_callback<__list<_Ts...>, _T> {
      using __t = __list<_Ts...>;
    };
    template<typename _List, typename _T>
      requires
        //__basic<_T> &&
        _List::template __contains<_T>
    struct __add_callback<_List, std::optional<_T>> {
      using __t = _List;
    };
    //  Callback not in list, but is an optional
    template<typename _List, typename _T>
      //requires __basic<_T>
    struct __add_callback<_List, std::optional<_T>> {
      using __t = __add_callback<_List, _T>::__t;
    };
    //  Callback not in list, but is a variant
    template<typename _List, typename _T>
      //requires __basic<_T>
    struct __add_callback<_List, std::variant<_T>> {
      using __t = __add_callback<_List, _T>::__t;
    };
    template<typename _List, typename _T, typename... _Us>
      //requires __basic<_T>
    struct __add_callback<_List, std::variant<_T, _Us...>> {
      using __t = __add_callback<
        typename __add_callback<
          _List,
          std::variant<_Us...>>::__t,
        _T>::__t;
    };
    //  Callback not in list, but it returns void
    template<typename... _Ts, typename _T>
      requires
        __basic<_T> &&
        (!__list<_Ts...>::template __contains<_T>) &&
        std::is_same_v<
          std::invoke_result_t<_T>,
          void>
    struct __add_callback<__list<_Ts...>, _T> {
      using __t = __list<_Ts..., _T>;
    };
    //  Callback not in list, and it returns non-void
    template<typename... _Ts, typename _T>
      requires
        __basic<_T> &&
        (!__list<_Ts...>::template __contains<_T>) &&
        (!std::is_same_v<
          std::invoke_result_t<_T>,
          void>)
    struct __add_callback<__list<_Ts...>, _T> {
      using __t = __add_callback<
        __list<_Ts..., _T>,
        std::invoke_result_t<_T>>::__t;
    };

    template<typename _T>
    using __invocables = __add_callback<__list<>, _T>::__t;

    template<typename _T>
    class __impl {
      using __invocables = __tramp::__invocables<_T>;
      using __variant = __invocables::__variant_t;
      __variant __v;
      template<typename... _Us>
      constexpr bool __handle(std::variant<_Us...>&& __v) noexcept {
        std::visit(
          [&](auto&& f) noexcept {
            this->__v.template emplace<
              std::remove_cvref_t<decltype(f)>>(
                static_cast<decltype(f)&&>(f));
          },
          std::move(__v));
        return false;
      }
      template<typename _U>
      constexpr bool __handle(std::optional<_U>&& __o) noexcept {
        if (!__o) {
          return true;
        }
        return __handle(*std::move(__o));
      }
      template<typename _U>
        requires std::is_invocable_v<_U>
      constexpr bool __handle(_U&& __f) noexcept {
        __v.template emplace<std::remove_cvref_t<_U>>(static_cast<_U&&>(__f));
        return false;
      }
    public:
      constexpr explicit __impl(_T __t) noexcept : __v(std::move(__t)) {}
      constexpr bool operator()() & noexcept {
        return std::visit(
          [&](auto& __f) noexcept {
            if constexpr (
              std::is_same_v<
                std::invoke_result_t<decltype(std::move(__f))>,
                void>)
            {
              std::move(__f)();
              return true;
            } else {
              return __handle(std::move(__f)());
            }
          },
          __v);
      }
    };

  }  // namespace __tramp

  template<typename _T>
  concept __trampolinable = requires {
    typename __tramp::__invocables<_T>;
  };
  
  template<__trampolinable _T>
  constexpr void __trampoline(_T __t) noexcept {
    if constexpr (std::is_same_v<std::invoke_result_t<_T>, void>) {
      static_cast<_T&&>(__t)();
    } else {
      __tramp::__impl __impl(std::move(__t));
      while (!__impl());
    }
  }

}  // namespace STDEXEC

#include "__epilogue.hpp"
