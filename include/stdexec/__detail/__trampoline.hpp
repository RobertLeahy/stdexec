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

#include "__concepts.hpp"
#include "__utility.hpp"
#include "__variant.hpp"

#include <type_traits>

#include "__prologue.hpp"

namespace STDEXEC
{
  namespace __tramp
  {
    struct __done
    {};

    template <class... _Ts>
    struct __types
    {};

    template <class _Ty>
    struct __is_variant : std::false_type
    {};

    template <class... _Ts>
    struct __is_variant<__variant<_Ts...>> : std::true_type
    {};

    template <class _Ty>
    concept __variant_unit = __is_variant<__decay_t<_Ty>>::value;

    template <class _Ty>
    concept __callable_unit = __nothrow_callable<_Ty>;

    template <class _Ty>
    concept __optional_unit =
      !__callable_unit<_Ty>
      && requires(_Ty&& __unit) {
           { static_cast<bool>(__unit) } noexcept -> __same_as<bool>;
           { *static_cast<_Ty&&>(__unit) } noexcept;
         }
      && __nothrow_callable<decltype(*__declval<_Ty>())>;

    template <class _Ty>
    concept __unit = __callable_unit<_Ty> || __optional_unit<_Ty> || __variant_unit<_Ty>;

    template <class _Ty, bool = __callable_unit<_Ty>>
    struct __unit_result;

    template <class _Ty>
    struct __unit_result<_Ty, true>
    {
      using __t = __call_result_t<_Ty>;
    };

    template <class _Ty>
    struct __unit_result<_Ty, false>
    {
      using __t = __call_result_t<decltype(*__declval<_Ty>())>;
    };

    template <class _Ty>
    using __unit_result_t = typename __unit_result<_Ty>::__t;

    template <class _Ty, class _Seen, bool = __unit<_Ty>>
    struct __trampoline_unit_impl : std::false_type
    {};

    template <class _Ty, class _Seen>
    struct __trampoline_variant_impl : std::false_type
    {};

    template <class... _Ts, class _Seen>
    struct __trampoline_variant_impl<__variant<_Ts...>, _Seen>
      : std::bool_constant<(__trampoline_unit_impl<_Ts, _Seen>::value && ...)>
    {};

    template <bool _IsVoid, bool _IsCycle, class _Next, class _Ty, class _Seen>
    struct __trampoline_unit_next : std::false_type
    {};

    template <bool _IsCycle, class _Next, class _Ty, class _Seen>
    struct __trampoline_unit_next<true, _IsCycle, _Next, _Ty, _Seen> : std::true_type
    {};

    template <class _Next, class _Ty, class... _Seen>
    struct __trampoline_unit_next<false, true, _Next, _Ty, __types<_Seen...>> : std::true_type
    {};

    template <class _Next, class _Ty, class... _Seen>
    struct __trampoline_unit_next<false, false, _Next, _Ty, __types<_Seen...>>
      : __trampoline_unit_impl<_Next, __types<_Seen..., _Ty>>
    {};

    template <class _Ty, class... _Seen>
    struct __trampoline_unit_impl<_Ty, __types<_Seen...>, true>
      : __trampoline_unit_next<std::is_void_v<__unit_result_t<_Ty>>,
                               __one_of<__unit_result_t<_Ty>, _Seen..., _Ty>,
                               __unit_result_t<_Ty>,
                               _Ty,
                               __types<_Seen...>>
    {};

    template <class _Ty, class... _Seen>
      requires __variant_unit<_Ty>
    struct __trampoline_unit_impl<_Ty, __types<_Seen...>, true>
      : __trampoline_variant_impl<__decay_t<_Ty>, __types<_Seen...>>
    {};

    template <class _Ty>
    concept __trampoline_unit = __trampoline_unit_impl<_Ty, __types<>>::value;

    template <class _Ty, class _Seen>
    struct __chain;

    template <class... _Ts>
    struct __concat_types;

    template <>
    struct __concat_types<>
    {
      using __t = __types<>;
    };

    template <class... _Ts>
    struct __concat_types<__types<_Ts...>>
    {
      using __t = __types<_Ts...>;
    };

    template <class... _Ts, class... _Us, class... _Rest>
    struct __concat_types<__types<_Ts...>, __types<_Us...>, _Rest...>
      : __concat_types<__types<_Ts..., _Us...>, _Rest...>
    {};

    template <class _Ty, class _Seen>
    struct __variant_chain;

    template <class... _Ts, class _Seen>
    struct __variant_chain<__variant<_Ts...>, _Seen>
    {
      using __t = typename __concat_types<typename __chain<_Ts, _Seen>::__t...>::__t;
    };

    template <class... _Ts>
    struct __prepend
    {};

    template <class _Ty, class... _Ts>
    struct __prepend<_Ty, __types<_Ts...>>
    {
      using __t = __types<_Ty, _Ts...>;
    };

    template <bool _IsVoid, bool _IsNew, class _Next, class _Ty, class _Seen>
    struct __chain_result;

    template <bool _IsNew, class _Next, class _Ty, class... _Seen>
    struct __chain_result<true, _IsNew, _Next, _Ty, __types<_Seen...>>
    {
      using __t = __types<>;
    };

    template <class _Next, class _Ty, class... _Seen>
    struct __chain_result<false, true, _Next, _Ty, __types<_Seen...>>
    {
      static_assert(__trampoline_unit<_Next>);

      using __t = typename __prepend<_Next,
                                     typename __chain<_Next, __types<_Seen..., _Ty>>::__t>::__t;
    };

    template <class _Next, class _Ty, class... _Seen>
    struct __chain_result<false, false, _Next, _Ty, __types<_Seen...>>
    {
      using __t = __types<_Next>;
    };

    template <class _Ty, class... _Seen>
    struct __chain<_Ty, __types<_Seen...>>
    {
      using __next = __unit_result_t<_Ty>;
      using __t = typename __chain_result<std::is_void_v<__next>,
                                          !__one_of<__next, _Seen..., _Ty>,
                                          __next,
                                          _Ty,
                                          __types<_Seen...>>::__t;
    };

    template <class _Ty, class... _Seen>
      requires __variant_unit<_Ty>
    struct __chain<_Ty, __types<_Seen...>>
    {
      using __t = typename __variant_chain<__decay_t<_Ty>, __types<_Seen...>>::__t;
    };

    template <class _Ty>
    using __continuations_t = typename __chain<_Ty, __types<>>::__t;

    template <class _Types>
    struct __as_variant;

    template <class... _Ts>
    struct __as_variant<__types<_Ts...>>
    {
      using __t = __uniqued_variant<__done, _Ts...>;
    };

    template <class _Ty>
    using __variant_t = typename __as_variant<__continuations_t<_Ty>>::__t;

    template <class _Unit, class _Variant>
      requires __trampoline_unit<_Unit>
    constexpr auto __advance(_Unit& __unit, _Variant& __out) noexcept -> bool;

    template <class _Variant>
    struct __step
    {
      _Variant& __out_;

      constexpr auto operator()(__done) const noexcept -> bool
      {
        return false;
      }

      template <class _Unit>
        requires __trampoline_unit<_Unit>
      constexpr auto operator()(_Unit& __unit) const noexcept -> bool
      {
        return __advance(__unit, __out_);
      }
    };

    template <class _Unit, class _Variant>
      requires __trampoline_unit<_Unit>
    constexpr auto __advance(_Unit& __unit, _Variant& __out) noexcept -> bool
    {
      if constexpr (__variant_unit<_Unit>)
      {
        if (__unit.__is_valueless())
        {
          return false;
        }
        return __visit(__step<_Variant>{__out}, __unit);
      }
      else if constexpr (__optional_unit<_Unit>)
      {
        if (!static_cast<bool>(__unit))
        {
          return false;
        }

        if constexpr (std::is_void_v<__unit_result_t<_Unit>>)
        {
          (*static_cast<_Unit&&>(__unit))();
          return false;
        }
        else
        {
          __out.__emplace_from([&]() noexcept -> __unit_result_t<_Unit> {
            return (*static_cast<_Unit&&>(__unit))();
          });
          return true;
        }
      }
      else
      {
        if constexpr (std::is_void_v<__unit_result_t<_Unit>>)
        {
          static_cast<_Unit&&>(__unit)();
          return false;
        }
        else
        {
          __out.__emplace_from([&]() noexcept -> __unit_result_t<_Unit> {
            return static_cast<_Unit&&>(__unit)();
          });
          return true;
        }
      }
    }

    template <__trampoline_unit _Ty>
    constexpr void __run(_Ty& __unit) noexcept
    {
      using __variant_t = __tramp::__variant_t<_Ty>;
      __variant_t __vars[] = {__variant_t{__no_init}, __variant_t{__no_init}};
      int         __current = 0;
      int         __next    = 1;

      for (bool __keep_going = __advance(__unit, __vars[__current]); __keep_going;)
      {
        __keep_going = __visit(__step<__variant_t>{__vars[__next]}, __vars[__current]);
        __vars[__current].template emplace<__done>();
        __current = 1 - __current;
        __next    = 1 - __next;
      }
    }

    template <class _Fn, class... _As>
      requires __trampoline_unit<__call_result_t<_Fn, _As...>>
    constexpr void __run_from(_Fn&& __fn, _As&&... __as) noexcept
    {
      using __unit_t = __call_result_t<_Fn, _As...>;
      using __variant_t = __tramp::__variant_t<__unit_t>;
      __variant_t __vars[] = {__variant_t{__no_init}, __variant_t{__no_init}};
      int         __current = 0;
      int         __next    = 1;

      bool __keep_going = [&]() noexcept {
        __unit_t __unit(static_cast<_Fn&&>(__fn)(static_cast<_As&&>(__as)...));
        return __advance(__unit, __vars[__current]);
      }();

      for (; __keep_going;)
      {
        __keep_going = __visit(__step<__variant_t>{__vars[__next]}, __vars[__current]);
        __vars[__current].template emplace<__done>();
        __current = 1 - __current;
        __next    = 1 - __next;
      }
    }
  }  // namespace __tramp

  template <class _Ty>
  concept __trampoline_unit = __tramp::__trampoline_unit<_Ty>;

  template <class _Ty>
  concept __trampolinable = __trampoline_unit<_Ty>;

  template <class _Fun, class... _As>
  concept __trampoline_invocable = __nothrow_callable<_Fun, _As...>
                                && (std::is_void_v<__call_result_t<_Fun, _As...>>
                                    || __trampoline_unit<__call_result_t<_Fun, _As...>>);

  template <class _Fun, class... _As>
    requires __trampoline_invocable<_Fun, _As...>
  constexpr void __trampoline(_Fun&& __fun, _As&&... __as) noexcept
  {
    if constexpr (std::is_void_v<__call_result_t<_Fun, _As...>>)
    {
      static_cast<_Fun&&>(__fun)(static_cast<_As&&>(__as)...);
    }
    else
    {
      __tramp::__run_from(static_cast<_Fun&&>(__fun), static_cast<_As&&>(__as)...);
    }
  }

  template <__trampoline_unit _Ty>
  struct __deferred_trampoline
  {
    template <class _Fun, class... _As>
      requires __nothrow_callable<_Fun, _As...>
            && __nothrow_constructible_from<_Ty, __call_result_t<_Fun, _As...>>
    explicit constexpr __deferred_trampoline(_Fun&& __fun, _As&&... __as) noexcept
      : __fun_(static_cast<_Fun&&>(__fun)(static_cast<_As&&>(__as)...))
    {}

    __deferred_trampoline(__deferred_trampoline const&)                    = delete;
    __deferred_trampoline(__deferred_trampoline&&)                         = delete;
    auto operator=(__deferred_trampoline const&) -> __deferred_trampoline& = delete;
    auto operator=(__deferred_trampoline&&) -> __deferred_trampoline&      = delete;

    constexpr ~__deferred_trampoline()
    {
      STDEXEC::__trampoline(static_cast<_Ty&&>(__fun_));
    }

    STDEXEC_ATTRIBUTE(no_unique_address) _Ty __fun_;
  };

  template <class _Fun, class... _As>
    requires __nothrow_callable<_Fun, _As...>
          && __trampoline_unit<__call_result_t<_Fun, _As...>>
  STDEXEC_HOST_DEVICE_DEDUCTION_GUIDE __deferred_trampoline(_Fun&&, _As&&...)
    -> __deferred_trampoline<__call_result_t<_Fun, _As...>>;
}  // namespace STDEXEC

#include "__epilogue.hpp"
