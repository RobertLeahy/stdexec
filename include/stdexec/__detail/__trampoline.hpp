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

#include "__prologue.hpp"

namespace STDEXEC
{
  template <__nothrow_callable _Ty>
  struct __trampoline
  {
    template <class _Fun, class... _As>
      requires __nothrow_callable<_Fun, _As...>
            && __nothrow_constructible_from<_Ty, __call_result_t<_Fun, _As...>>
    explicit constexpr __trampoline(_Fun&& __fun, _As&&... __as) noexcept
      : __fun_(static_cast<_Fun&&>(__fun)(static_cast<_As&&>(__as)...))
    {}

    __trampoline(__trampoline const&)                    = delete;
    __trampoline(__trampoline&&)                         = delete;
    auto operator=(__trampoline const&) -> __trampoline& = delete;
    auto operator=(__trampoline&&) -> __trampoline&      = delete;

    constexpr ~__trampoline()
    {
      static_cast<_Ty&&>(__fun_)();
    }

    STDEXEC_ATTRIBUTE(no_unique_address) _Ty __fun_;
  };

  template <class _Fun, class... _As>
    requires __nothrow_callable<_Fun, _As...>
  STDEXEC_HOST_DEVICE_DEDUCTION_GUIDE __trampoline(_Fun&&, _As&&...)
    -> __trampoline<__call_result_t<_Fun, _As...>>;
}  // namespace STDEXEC

#include "__epilogue.hpp"
