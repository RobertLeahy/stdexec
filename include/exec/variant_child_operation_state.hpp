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

#include <algorithm>
#include <cstddef>
#include <memory>
#include <new>
#include <type_traits>
#include <utility>
#include "like_t.hpp"
#include "../stdexec/execution.hpp"

namespace exec {

template<
  typename ParentOp,
  template<typename> typename Tag,
  typename Env,
  typename... ChildSenders>
class variant_child_operation_state {
private:
  template<typename ChildSender>
  class receiver_ {
  public:
    using receiver_concept = ::stdexec::receiver_t;
    template<typename ChildOp>
    constexpr static receiver_ make_receiver_for(ChildOp* child_op) noexcept {
      static_assert(std::same_as<ChildOp, child_<ChildSender>>);
      const auto self = std::launder(
        reinterpret_cast<variant_child_operation_state*>(child_op));
      const auto parent = static_cast<ParentOp*>(self);
      return receiver_(parent);
    }
    template<typename... Vs>
    constexpr void set_value(Vs&&... vs) noexcept {
      parent_op_->set_value(Tag<ChildSender>{}, std::forward<Vs>(vs)...);
    }
    template<typename E>
    constexpr void set_error(E&& e) noexcept {
      parent_op_->set_error(Tag<ChildSender>{}, std::forward<E>(e));
    }
    template<typename... Args>
    constexpr void set_stopped(const Args&...) noexcept {
      parent_op_->set_stopped(Tag<ChildSender>{});
    }
    constexpr Env get_env() const noexcept {
      return parent_op_->get_env(Tag<ChildSender>{});
    }
    constexpr explicit receiver_(ParentOp* parent_op) noexcept
      : parent_op_(parent_op)
    {}
  private:
    ParentOp* parent_op_;
  };
  template<typename ChildSender>
  using child_ = ::stdexec::connect_result_t<
    ChildSender,
    receiver_<ChildSender>>;
  constexpr static auto size_ = std::max({sizeof(child_<ChildSenders>)...});
  constexpr static auto alignment_ = std::max(
    {alignof(child_<ChildSenders>)...});
  alignas(alignment_) std::byte buffer_[size_];
  template<typename ChildSender>
  constexpr static bool check_ =
    (std::is_same_v<ChildSender, ChildSenders> || ...);
public:
  template<typename ChildSender, typename Self>
    requires check_<ChildSender>
  constexpr decltype(auto) get(this Self&& self) noexcept {
    using operation_state = child_<ChildSender>;
    return ::stdexec::__forward_like<Self>(
      *std::launder(
        reinterpret_cast<
          std::remove_reference_t<exec::like_t<Self, child_<ChildSender>>>*>(
            self.buffer_)));
  }
  template<typename ChildSender>
    requires check_<ChildSender>
  constexpr void start() noexcept {
    ::stdexec::start(get<ChildSender>());
  }
  template<typename ChildSender>
    requires check_<ChildSender>
  constexpr void construct(ChildSender&& sender) noexcept(
    noexcept(
      ::stdexec::connect(
        std::forward<ChildSender>(sender),
        std::declval<receiver_<ChildSender>>())))
  {
    const auto parent_op = static_cast<ParentOp*>(this);
    ::new (buffer_) child_<ChildSender>(
      ::stdexec::connect(
        std::forward<ChildSender>(sender),
        receiver_<ChildSender>(parent_op)));
  }
  template<typename ChildSender>
    requires check_<ChildSender>
  constexpr void destruct() noexcept {
    get<ChildSender>().~child_<ChildSender>();
  }
};

}  // namespace exec
