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

#include "../stdexec/execution.hpp"

namespace exec {

template<typename Derived, ::stdexec::receiver Receiver>
class inlinable_operation_state {
  Receiver r_;
protected:
  constexpr explicit inlinable_operation_state(Receiver r) noexcept
    : r_((Receiver&&)r)
  {}
  constexpr Receiver& get_receiver() noexcept {
    return r_;
  }
};

template<typename Derived, ::stdexec::receiver Receiver>
  requires ::stdexec::inlinable_receiver<Receiver, Derived>
class inlinable_operation_state<Derived, Receiver> {
protected:
  constexpr explicit inlinable_operation_state(const Receiver&) noexcept {}
  constexpr Receiver get_receiver() noexcept {
    return Receiver::make_receiver_for(static_cast<Derived*>(this));
  }
};

} // namespace exec
