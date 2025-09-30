/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *                         Copyright (c) 2025 Robert Leahy. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 *
 * Licensed under the Apache License, Version 2.0 with LLVM Exceptions (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 * https://llvm.org/LICENSE.txt
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#pragma once

#include <cstddef>
#include <memory>
#include <tuple>
#include <utility>

#include "object.hpp"
#include "../stdexec/execution.hpp"

namespace exec {

template<object Object>
class storage_for_object {
  using type_ = typename Object::type;
  alignas(type_) std::byte storage_[sizeof(type_)];
public:
  constexpr void* get_uninitialized() noexcept {
    return storage_;
  }
  constexpr type_* get_initialized() noexcept {
    return std::launder(reinterpret_cast<type_*>(storage_));
  }
  constexpr ::stdexec::sender auto construct(Object& o) noexcept(
    noexcept(o.construct(get_uninitialized())))
  {
    return o.construct(get_uninitialized());
  }
  constexpr ::stdexec::sender auto destroy(Object&& o) noexcept(
    noexcept(std::move(o).destroy(get_initialized())))
  {
    return std::move(o).destroy(get_initialized());
  }
  constexpr std::tuple<type_&> get_argument() noexcept {
    return std::forward_as_tuple(*get_initialized());
  }
};

template<void_object Object>
struct storage_for_object<Object> {
  constexpr ::stdexec::sender auto construct(Object& o) noexcept(
    noexcept(o.construct()))
  {
    return o.construct();
  }
  constexpr ::stdexec::sender auto destroy(Object&& o) noexcept(
    noexcept(o.destroy()))
  {
    return std::move(o).destroy();
  }
  constexpr std::tuple<> get_argument() noexcept {
    return {};
  }
};

}  // namespace exec
