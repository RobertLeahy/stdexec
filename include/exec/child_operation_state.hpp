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

#include <new>
#include <utility>
#include "../stdexec/execution.hpp"

namespace exec {

template<typename ParentOp, typename Tag, typename Env, typename ChildSender>
class manual_child_operation_state {
private:
  class receiver_ {
  public:
    using receiver_concept = ::stdexec::receiver_t;
    template<typename ChildOp>
    constexpr static receiver_ make_receiver_for(ChildOp* child_op) noexcept {
      static_assert(std::same_as<ChildOp, child_>);
      //  TODO: Jens says these need to change
      const auto storage = reinterpret_cast<unsigned char*>(child_op);
      const auto self = reinterpret_cast<manual_child_operation_state*>(
        storage);
      const auto parent_op = static_cast<ParentOp*>(self);
      return receiver_(parent_op);
    }
    template<typename... Vs>
    constexpr void set_value(Vs&&... vs) noexcept {
      parent_op_->set_value(Tag{}, std::forward<Vs>(vs)...);
    }
    template<typename E>
    constexpr void set_error(E&& e) noexcept {
      parent_op_->set_error(Tag{}, std::forward<E>(e));
    }
    template<typename... Args>
    constexpr void set_stopped(const Args&...) noexcept {
      parent_op_->set_stopped(Tag{});
    }
    constexpr Env get_env() const noexcept {
      return parent_op_->get_env(Tag{});
    }
    constexpr explicit receiver_(ParentOp* parent_op) noexcept
      : parent_op_(parent_op)
    {}
  private:
    ParentOp* parent_op_;
  };
  using child_ = ::stdexec::connect_result_t<ChildSender, receiver_>;
public:
  manual_child_operation_state() noexcept = default;
  ~manual_child_operation_state() = default;
  constexpr void start() noexcept {
    ::stdexec::start(get());
  }
  constexpr void construct(ChildSender&& sender) noexcept(
    noexcept(
      ::stdexec::connect(
        std::forward<ChildSender>(sender),
        std::declval<receiver_>())))
  {
    const auto parent_op = static_cast<ParentOp*>(this);
    ::new (&storage_) child_(
      ::stdexec::connect(
        std::forward<ChildSender>(sender),
        receiver_(parent_op)));
  }
  constexpr void destruct() noexcept {
    get().~child_();
  }
private:
  child_& get() noexcept {
    return *std::launder(reinterpret_cast<child_*>(&storage_));
  }
  alignas(child_) unsigned char storage_[sizeof(child_)];
};

template<typename ParentOp, typename Tag, typename Env, typename ChildSender>
class child_operation_state
  : public manual_child_operation_state<ParentOp, Tag, Env, ChildSender>
{
  using base_ = manual_child_operation_state<ParentOp, Tag, Env, ChildSender>;
  using base_::construct;
  using base_::destruct;
public:
  constexpr explicit child_operation_state(ChildSender&& sender)
    noexcept(
      noexcept(
        base_::construct(std::forward<ChildSender>(sender))))
  {
    base_::construct(std::forward<ChildSender>(sender));
  }
  constexpr ~child_operation_state() noexcept {
    base_::destruct();
  }
};

}  // namespace exec
