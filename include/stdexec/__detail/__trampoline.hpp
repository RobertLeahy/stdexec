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
    using __next_t = __call_result_t<_Ty>;

    template <class _Ty, class _Seen, bool = __nothrow_callable<_Ty>>
    struct __trampolinable_impl : std::false_type
    {};

    template <bool _IsVoid, bool _IsCycle, class _Next, class _Ty, class _Seen>
    struct __trampolinable_next : std::false_type
    {};

    template <bool _IsCycle, class _Next, class _Ty, class _Seen>
    struct __trampolinable_next<true, _IsCycle, _Next, _Ty, _Seen> : std::true_type
    {};

    template <class _Next, class _Ty, class... _Seen>
    struct __trampolinable_next<false, false, _Next, _Ty, __types<_Seen...>>
      : __trampolinable_impl<_Next, __types<_Seen..., _Ty>>
    {};

    template <class _Ty, class... _Seen>
    struct __trampolinable_impl<_Ty, __types<_Seen...>, true>
      : __trampolinable_next<std::is_void_v<__next_t<_Ty>>,
                             __one_of<__next_t<_Ty>, _Seen..., _Ty>,
                             __next_t<_Ty>,
                             _Ty,
                             __types<_Seen...>>
    {};

    template <class _Ty>
    concept __trampolinable = __trampolinable_impl<_Ty, __types<>>::value;

    template <class _Ty, class _Seen>
    struct __chain;

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
      static_assert(__trampolinable<_Next>);

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
      using __next = __next_t<_Ty>;
      using __t = typename __chain_result<std::is_void_v<__next>,
                                          !__one_of<__next, _Seen..., _Ty>,
                                          __next,
                                          _Ty,
                                          __types<_Seen...>>::__t;
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

    template <class _Variant>
    struct __step
    {
      _Variant& __out_;

      constexpr auto operator()(__done) const noexcept -> bool
      {
        return false;
      }

      template <class _Fun>
        requires __trampolinable<_Fun>
      constexpr auto operator()(_Fun& __fun) const noexcept -> bool
      {
        if constexpr (std::is_void_v<__next_t<_Fun>>)
        {
          static_cast<_Fun&&>(__fun)();
          return false;
        }
        else
        {
          __out_.__emplace_from([&]() noexcept -> __next_t<_Fun> {
            return static_cast<_Fun&&>(__fun)();
          });
          return true;
        }
      }
    };

    template <__trampolinable _Ty>
    constexpr void __run(_Ty& __fun) noexcept
    {
      if constexpr (std::is_void_v<__next_t<_Ty>>)
      {
        static_cast<_Ty&&>(__fun)();
      }
      else
      {
        using __variant_t = __tramp::__variant_t<_Ty>;
        __variant_t __vars[] = {__variant_t{__no_init}, __variant_t{__no_init}};
        int         __current = 0;
        int         __next    = 1;

        __vars[__current].__emplace_from([&]() noexcept -> __next_t<_Ty> {
          return static_cast<_Ty&&>(__fun)();
        });

        for (bool __keep_going = true; __keep_going;)
        {
          __keep_going = __visit(__step<__variant_t>{__vars[__next]}, __vars[__current]);
          __vars[__current].template emplace<__done>();
          __current = 1 - __current;
          __next    = 1 - __next;
        }
      }
    }
  }  // namespace __tramp

  template <class _Ty>
  concept __trampolinable = __tramp::__trampolinable<_Ty>;

  template <class _Fun, class... _As>
  concept __trampoline_invocable = __nothrow_callable<_Fun, _As...>
                                && (std::is_void_v<__call_result_t<_Fun, _As...>>
                                    || __trampolinable<__call_result_t<_Fun, _As...>>);

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
      __call_result_t<_Fun, _As...> __first(
        static_cast<_Fun&&>(__fun)(static_cast<_As&&>(__as)...));
      __tramp::__run(__first);
    }
  }

  template <__trampolinable _Ty>
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
          && __trampolinable<__call_result_t<_Fun, _As...>>
  STDEXEC_HOST_DEVICE_DEDUCTION_GUIDE __deferred_trampoline(_Fun&&, _As&&...)
    -> __deferred_trampoline<__call_result_t<_Fun, _As...>>;
}  // namespace STDEXEC

#include "__epilogue.hpp"
