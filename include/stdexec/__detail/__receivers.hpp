/*
 * Copyright (c) 2022-2024 NVIDIA Corporation
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

#include "__execution_fwd.hpp"

#include "__concepts.hpp"
#include "__diagnostics.hpp"
#include "__env.hpp"
#include "__tag_invoke.hpp"
#include "__trampoline.hpp"

#include "../functional.hpp"

#include <exception>
#include <optional>
#include <variant>

#include "__prologue.hpp"

namespace STDEXEC
{
  namespace __detail
  {
    template <__disposition _Disposition>
    struct __completion_tag
    {
      static constexpr STDEXEC::__disposition __disposition = _Disposition;

      template <STDEXEC::__disposition _OtherDisposition>
      constexpr bool operator==(__completion_tag<_OtherDisposition>) const noexcept
      {
        return _Disposition == _OtherDisposition;
      }
    };
  }  // namespace __detail

  /////////////////////////////////////////////////////////////////////////////
  // [execution.receivers]
  template <class _Receiver, class... _As>
  concept __set_value_member = requires(_Receiver &&__rcvr, _As &&...__args) {
    static_cast<_Receiver &&>(__rcvr).set_value(static_cast<_As &&>(__args)...);
  };

  template <class _Receiver, class... _As>
  using __set_value_result_t = decltype(__declval<_Receiver>().set_value(__declval<_As>()...));

  struct set_value_or_defer_t : __detail::__completion_tag<__disposition::__value>
  {
    template <class _Fn, class... _As>
    using __f = __minvoke<_Fn, _As...>;

    template <class _Receiver, class... _As>
      requires __set_value_member<_Receiver, _As...>
            && __same_as<__set_value_result_t<_Receiver, _As...>, void>
    STDEXEC_ATTRIBUTE(host, device, always_inline)
    constexpr void operator()(_Receiver &&__rcvr, _As &&...__as) const noexcept
    {
      static_assert(noexcept(
                      static_cast<_Receiver &&>(__rcvr).set_value(static_cast<_As &&>(__as)...)),
                    "set_value member functions must be noexcept");
      static_cast<_Receiver &&>(__rcvr).set_value(static_cast<_As &&>(__as)...);
    }

    template <class _Receiver, class... _As>
      requires __set_value_member<_Receiver, _As...>
            && __trampolinable<__set_value_result_t<_Receiver, _As...>>
    [[nodiscard]]
    STDEXEC_ATTRIBUTE(host, device, always_inline)
    constexpr auto operator()(_Receiver &&__rcvr, _As &&...__as) const noexcept
      -> __set_value_result_t<_Receiver, _As...>
    {
      static_assert(noexcept(
                      static_cast<_Receiver &&>(__rcvr).set_value(static_cast<_As &&>(__as)...)),
                    "set_value member functions must be noexcept");
      return static_cast<_Receiver &&>(__rcvr).set_value(static_cast<_As &&>(__as)...);
    }
  };

  inline constexpr set_value_or_defer_t set_value_or_defer{};

  template <class _Receiver, class... _As>
  using set_value_result_t = __call_result_t<set_value_or_defer_t, _Receiver, _As...>;

  template <class _Receiver, class... _As>
  inline constexpr bool set_value_defers_v =
    !__same_as<set_value_result_t<_Receiver, _As...>, void>;

  struct set_value_t : __detail::__completion_tag<__disposition::__value>
  {
    using __defer_t = set_value_or_defer_t;

    template <class _Fn, class... _As>
    using __f = __minvoke<_Fn, _As...>;

    template <class _Receiver, class... _As>
      requires __callable<set_value_or_defer_t, _Receiver, _As...>
    STDEXEC_ATTRIBUTE(host, device, always_inline)
    constexpr void operator()(_Receiver &&__rcvr, _As &&...__as) const noexcept
    {
      STDEXEC::__trampoline([&]() noexcept -> decltype(auto) {
        return STDEXEC::set_value_or_defer(static_cast<_Receiver &&>(__rcvr),
                                           static_cast<_As &&>(__as)...);
      });
    }

    template <class _Receiver, class... _As>
      requires (!__callable<set_value_or_defer_t, _Receiver, _As...>)
            && __tag_invocable<set_value_t, _Receiver, _As...>
    [[deprecated("the use of tag_invoke for set_value is deprecated")]]
    STDEXEC_ATTRIBUTE(host, device, always_inline)  //
      constexpr void operator()(_Receiver &&__rcvr, _As &&...__as) const noexcept
    {
      static_assert(__nothrow_tag_invocable<set_value_t, _Receiver, _As...>);
      (void) __tag_invoke(*this, static_cast<_Receiver &&>(__rcvr), static_cast<_As &&>(__as)...);
    }
  };

  template <class _Receiver, class _Error>
  concept __set_error_member = requires(_Receiver &&__rcvr, _Error &&__err) {
    static_cast<_Receiver &&>(__rcvr).set_error(static_cast<_Error &&>(__err));
  };

  template <class _Receiver, class _Error>
  using __set_error_result_t = decltype(__declval<_Receiver>().set_error(__declval<_Error>()));

  struct set_error_or_defer_t : __detail::__completion_tag<__disposition::__error>
  {
    template <class _Fn, class... _Args>
      requires(sizeof...(_Args) == 1)
    using __f = __minvoke<_Fn, _Args...>;

    template <class _Receiver, class _Error>
      requires __set_error_member<_Receiver, _Error>
            && __same_as<__set_error_result_t<_Receiver, _Error>, void>
    STDEXEC_ATTRIBUTE(host, device, always_inline)
    constexpr void operator()(_Receiver &&__rcvr, _Error &&__err) const noexcept
    {
      static_assert(noexcept(
                      static_cast<_Receiver &&>(__rcvr).set_error(static_cast<_Error &&>(__err))),
                    "set_error member functions must be noexcept");
      static_cast<_Receiver &&>(__rcvr).set_error(static_cast<_Error &&>(__err));
    }

    template <class _Receiver, class _Error>
      requires __set_error_member<_Receiver, _Error>
            && __trampolinable<__set_error_result_t<_Receiver, _Error>>
    [[nodiscard]]
    STDEXEC_ATTRIBUTE(host, device, always_inline)
    constexpr auto operator()(_Receiver &&__rcvr, _Error &&__err) const noexcept
      -> __set_error_result_t<_Receiver, _Error>
    {
      static_assert(noexcept(
                      static_cast<_Receiver &&>(__rcvr).set_error(static_cast<_Error &&>(__err))),
                    "set_error member functions must be noexcept");
      return static_cast<_Receiver &&>(__rcvr).set_error(static_cast<_Error &&>(__err));
    }
  };

  inline constexpr set_error_or_defer_t set_error_or_defer{};

  template <class _Receiver, class _Error>
  using set_error_result_t = __call_result_t<set_error_or_defer_t, _Receiver, _Error>;

  template <class _Receiver, class _Error>
  inline constexpr bool set_error_defers_v =
    !__same_as<set_error_result_t<_Receiver, _Error>, void>;

  struct set_error_t : __detail::__completion_tag<__disposition::__error>
  {
    using __defer_t = set_error_or_defer_t;

    template <class _Fn, class... _Args>
      requires(sizeof...(_Args) == 1)
    using __f = __minvoke<_Fn, _Args...>;

    template <class _Receiver, class _Error>
      requires __callable<set_error_or_defer_t, _Receiver, _Error>
    STDEXEC_ATTRIBUTE(host, device, always_inline)
    constexpr void operator()(_Receiver &&__rcvr, _Error &&__err) const noexcept
    {
      STDEXEC::__trampoline([&]() noexcept -> decltype(auto) {
        return STDEXEC::set_error_or_defer(static_cast<_Receiver &&>(__rcvr),
                                           static_cast<_Error &&>(__err));
      });
    }

    template <class _Receiver, class _Error>
      requires (!__callable<set_error_or_defer_t, _Receiver, _Error>)
            && __tag_invocable<set_error_t, _Receiver, _Error>
    [[deprecated("the use of tag_invoke for set_error is deprecated")]]
    STDEXEC_ATTRIBUTE(host, device, always_inline)  //
      constexpr void operator()(_Receiver &&__rcvr, _Error &&__err) const noexcept
    {
      static_assert(__nothrow_tag_invocable<set_error_t, _Receiver, _Error>);
      (void) __tag_invoke(*this, static_cast<_Receiver &&>(__rcvr), static_cast<_Error &&>(__err));
    }
  };

  template <class _Receiver>
  concept __set_stopped_member = requires(_Receiver &&__rcvr) {
    static_cast<_Receiver &&>(__rcvr).set_stopped();
  };

  template <class _Receiver>
  using __set_stopped_result_t = decltype(__declval<_Receiver>().set_stopped());

  struct set_stopped_or_defer_t : __detail::__completion_tag<__disposition::__stopped>
  {
    template <class _Fn, class... _Args>
      requires(sizeof...(_Args) == 0)
    using __f = __minvoke<_Fn, _Args...>;

    template <class _Receiver>
      requires __set_stopped_member<_Receiver>
            && __same_as<__set_stopped_result_t<_Receiver>, void>
    STDEXEC_ATTRIBUTE(host, device, always_inline)
    constexpr void operator()(_Receiver &&__rcvr) const noexcept
    {
      static_assert(noexcept(static_cast<_Receiver &&>(__rcvr).set_stopped()),
                    "set_stopped member functions must be noexcept");
      static_cast<_Receiver &&>(__rcvr).set_stopped();
    }

    template <class _Receiver>
      requires __set_stopped_member<_Receiver>
            && __trampolinable<__set_stopped_result_t<_Receiver>>
    [[nodiscard]]
    STDEXEC_ATTRIBUTE(host, device, always_inline)
    constexpr auto operator()(_Receiver &&__rcvr) const noexcept -> __set_stopped_result_t<_Receiver>
    {
      static_assert(noexcept(static_cast<_Receiver &&>(__rcvr).set_stopped()),
                    "set_stopped member functions must be noexcept");
      return static_cast<_Receiver &&>(__rcvr).set_stopped();
    }
  };

  inline constexpr set_stopped_or_defer_t set_stopped_or_defer{};

  template <class _Receiver>
  using set_stopped_result_t = __call_result_t<set_stopped_or_defer_t, _Receiver>;

  template <class _Receiver>
  inline constexpr bool set_stopped_defers_v = !__same_as<set_stopped_result_t<_Receiver>, void>;

  struct set_stopped_t : __detail::__completion_tag<__disposition::__stopped>
  {
    using __defer_t = set_stopped_or_defer_t;

    template <class _Fn, class... _Args>
      requires(sizeof...(_Args) == 0)
    using __f = __minvoke<_Fn, _Args...>;

    template <class _Receiver>
      requires __callable<set_stopped_or_defer_t, _Receiver>
    STDEXEC_ATTRIBUTE(host, device, always_inline)
    constexpr void operator()(_Receiver &&__rcvr) const noexcept
    {
      STDEXEC::__trampoline([&]() noexcept -> decltype(auto) {
        return STDEXEC::set_stopped_or_defer(static_cast<_Receiver &&>(__rcvr));
      });
    }

    template <class _Receiver>
      requires (!__callable<set_stopped_or_defer_t, _Receiver>)
            && __tag_invocable<set_stopped_t, _Receiver>
    [[deprecated("the use of tag_invoke for set_stopped is deprecated")]]
    STDEXEC_ATTRIBUTE(host, device, always_inline)  //
      constexpr void operator()(_Receiver &&__rcvr) const noexcept
    {
      static_assert(__nothrow_tag_invocable<set_stopped_t, _Receiver>);
      (void) __tag_invoke(*this, static_cast<_Receiver &&>(__rcvr));
    }
  };

  inline constexpr set_value_t   set_value{};
  inline constexpr set_error_t   set_error{};
  inline constexpr set_stopped_t set_stopped{};

  struct receiver_tag
  {
    using receiver_concept = receiver_tag;  // NOT TO SPEC
  };

  namespace __detail
  {
    template <class _Receiver>
    concept __enable_receiver = (STDEXEC_PP_WHEN(
      STDEXEC_EDG(),
      requires {
        typename _Receiver::receiver_concept;
      } &&) __std::derived_from<typename _Receiver::receiver_concept, receiver_tag>);
  }  // namespace __detail

  template <class _Receiver>
  concept receiver = __detail::__enable_receiver<__decay_t<_Receiver>>
                  && __environment_provider<__cref_t<_Receiver>>
                  && __nothrow_move_constructible<__decay_t<_Receiver>>
                  && __std::constructible_from<__decay_t<_Receiver>, _Receiver>;

  struct _THE_RECEIVER_DOES_NOT_ACCEPT_ALL_OF_THE_SENDERS_COMPLETION_SIGNALS_
  {};

  namespace __detail
  {
    template <class _ValueResult, class _ErrorResult>
    struct __deferred_result
    {
      using __t = std::variant<_ValueResult, _ErrorResult>;
    };

    template <class _Result>
    struct __deferred_result<_Result, _Result>
    {
      using __t = _Result;
    };

    template <class _ErrorResult>
    struct __deferred_result<void, _ErrorResult>
    {
      using __t = std::optional<_ErrorResult>;
    };

    template <class _ValueResult>
    struct __deferred_result<_ValueResult, void>
    {
      using __t = std::optional<_ValueResult>;
    };

    template <>
    struct __deferred_result<void, void>
    {
      using __t = void;
    };

    template <class _Result>
    inline constexpr bool __is_optional_v = false;

    template <class _Result>
    inline constexpr bool __is_optional_v<std::optional<_Result>> = true;

    template <class _Result>
    inline constexpr bool __is_variant_v = false;

    template <class... _Results>
    inline constexpr bool __is_variant_v<std::variant<_Results...>> = true;

    template <class _Result, class _Fun>
    STDEXEC_ATTRIBUTE(host, device)
    constexpr auto __defer_result(_Fun &&__fun) -> _Result
    {
      using _FunResult = __call_result_t<_Fun>;

      if constexpr (__same_as<_Result, void>)
      {
        static_cast<_Fun &&>(__fun)();
      }
      else if constexpr (__same_as<_FunResult, void>)
      {
        static_cast<_Fun &&>(__fun)();
        return std::nullopt;
      }
      else if constexpr (__is_optional_v<_Result>)
      {
        return _Result{std::in_place, static_cast<_Fun &&>(__fun)()};
      }
      else if constexpr (__is_variant_v<_Result>)
      {
        return _Result{std::in_place_type<_FunResult>, static_cast<_Fun &&>(__fun)()};
      }
      else
      {
        return static_cast<_Fun &&>(__fun)();
      }
    }

    template <class _Receiver, class _Tag, class... _Args>
    constexpr auto __try_completion(_Tag (*)(_Args...))
      -> __mexception<_WHAT_(_CONCEPT_CHECK_FAILURE_),
                      _WHY_(_THE_RECEIVER_DOES_NOT_ACCEPT_ALL_OF_THE_SENDERS_COMPLETION_SIGNALS_),
                      _UNHANDLED_COMPLETION_SIGNAL_<_Tag(_Args...)>,
                      _WITH_RECEIVER_(_Receiver)>;

    template <class _Receiver, class _Tag, class... _Args>
      requires __callable<_Tag, _Receiver, _Args...>
    auto __try_completion(_Tag (*)(_Args...)) -> __msuccess;

    template <class _Receiver, class... _Sigs>
    constexpr auto __try_completions(completion_signatures<_Sigs...> *) -> decltype((
      __msuccess(),
      ...,
      __detail::__try_completion<__decay_t<_Receiver>>(static_cast<_Sigs *>(nullptr))));
  }  // namespace __detail

  template <class _Receiver, class _Completions>
  concept receiver_of = receiver<_Receiver> && requires(_Completions *__completions) {
    { __detail::__try_completions<_Receiver>(__completions) } -> __ok;
  };

  /// A utility for calling set_value with the result of a function invocation:
  template <class _Receiver, class _Fun, class... _As>
  STDEXEC_ATTRIBUTE(nodiscard, host, device)
  constexpr auto __set_value_from(_Receiver &&__rcvr, _Fun &&__fun, _As &&...__as) noexcept
    -> decltype(auto)
  {
    using _ValueResult = decltype([&]() noexcept -> decltype(auto) {
      if constexpr (__std::same_as<void, __invoke_result_t<_Fun, _As...>>)
      {
        return STDEXEC::set_value_or_defer(static_cast<_Receiver &&>(__rcvr));
      }
      else
      {
        return STDEXEC::set_value_or_defer(
          static_cast<_Receiver &&>(__rcvr),
          __invoke(static_cast<_Fun &&>(__fun), static_cast<_As &&>(__as)...));
      }
    }());

    if constexpr (__nothrow_invocable<_Fun, _As...>)
    {
      if constexpr (__std::same_as<void, __invoke_result_t<_Fun, _As...>>)
      {
        __invoke(static_cast<_Fun &&>(__fun), static_cast<_As &&>(__as)...);
        return STDEXEC::set_value_or_defer(static_cast<_Receiver &&>(__rcvr));
      }
      else
      {
        return STDEXEC::set_value_or_defer(
          static_cast<_Receiver &&>(__rcvr),
          __invoke(static_cast<_Fun &&>(__fun), static_cast<_As &&>(__as)...));
      }
    }
    else
    {
      using _ErrorResult = set_error_result_t<_Receiver, std::exception_ptr>;
      using _Result = typename __detail::__deferred_result<_ValueResult, _ErrorResult>::__t;

      STDEXEC_TRY
      {
        return __detail::__defer_result<_Result>([&]() -> decltype(auto) {
          if constexpr (__std::same_as<void, __invoke_result_t<_Fun, _As...>>)
          {
            __invoke(static_cast<_Fun &&>(__fun), static_cast<_As &&>(__as)...);
            return STDEXEC::set_value_or_defer(static_cast<_Receiver &&>(__rcvr));
          }
          else
          {
            return STDEXEC::set_value_or_defer(
              static_cast<_Receiver &&>(__rcvr),
              __invoke(static_cast<_Fun &&>(__fun), static_cast<_As &&>(__as)...));
          }
        });
      }
      STDEXEC_CATCH_ALL
      {
        return __detail::__defer_result<_Result>([&]() noexcept -> decltype(auto) {
          return STDEXEC::set_error_or_defer(static_cast<_Receiver &&>(__rcvr),
                                             std::current_exception());
        });
      }
    }
  }

  template <class _Tag, class _Receiver>
  constexpr auto __mk_completion_fn(_Tag, _Receiver &__rcvr) noexcept
  {
    return [&]<class... _Args>(_Args &&...__args) noexcept
    {
      _Tag()(static_cast<_Receiver &&>(__rcvr), static_cast<_Args &&>(__args)...);
    };
  }

  // Used to test whether a sender has a nothrow connect to a receiver whose environment
  // is _Env..., or if _Env... is empty (indicating that the sender is non-dependent), to
  // a receiver with an arbitrary environment.
  struct __receiver_archetype_base
  {
    using receiver_concept = receiver_tag;

    template <class... _Args>
    STDEXEC_ATTRIBUTE(host, device)
    constexpr void set_value(_Args &&...) noexcept
    {}

    template <class _Error>
    STDEXEC_ATTRIBUTE(host, device)
    constexpr void set_error(_Error &&) noexcept
    {}

    STDEXEC_ATTRIBUTE(host, device)
    constexpr void set_stopped() noexcept {}
  };

  template <class _Env>
  struct __receiver_archetype : __receiver_archetype_base
  {
    STDEXEC_ATTRIBUTE(nodiscard, noreturn, host, device)
    auto get_env() const noexcept -> _Env
    {
      STDEXEC_ASSERT(false);
      STDEXEC_TERMINATE();
    }
  };
}  // namespace STDEXEC

#include "__epilogue.hpp"
