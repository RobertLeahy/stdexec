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

#include <type_traits>

#include "is_nothrow_connectable.hpp"
#include "../stdexec/execution.hpp"

namespace exec {

template<typename Sender>
concept exit_sender =
  ::stdexec::sender<Sender> &&
  std::is_nothrow_constructible_v<
    std::remove_cvref_t<Sender>,
    Sender> &&
  std::is_nothrow_move_constructible_v<
    std::remove_cvref_t<Sender>>;

template<typename Sender, typename Env>
concept exit_sender_in =
  exit_sender<Sender> &&
  ::stdexec::sender_in<Sender, Env> &&
  is_nothrow_connectable_v<Sender, Env> &&
  std::is_same_v<
    ::stdexec::completion_signatures<
      ::stdexec::set_value_t()>,
    ::stdexec::completion_signatures_of_t<
      Sender,
      Env>>;

}  // namespace exec
