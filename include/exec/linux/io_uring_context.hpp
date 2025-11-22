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

#if !__has_include(<linux/io_uring.h>)
#error "io_uring.h not found. Your kernel is probably too old."
#else
#include <linux/io_uring.h>

#include <algorithm>
#include <atomic>
#include <cassert>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <memory>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <errno.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "memory_mapped_region.hpp"
#include "safe_file_descriptor.hpp"
#include "../child_operation_state.hpp"
#include "../inlinable_operation_state.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

namespace detail::io_uring_context {

struct exception : std::system_error {
  explicit exception(const int err, std::string what)
    : std::system_error(
        std::error_code(
          err,
          std::generic_category())),
      what_(std::move(what))
  {}
  explicit exception(std::string what)
    : exception(errno, std::move(what))
  {}
  virtual const char* what() const noexcept override {
    return what_.c_str();
  }
private:
  std::string what_;
};

struct completion {
  virtual void complete(const ::io_uring_cqe&) noexcept = 0;
};

struct initiation {
  virtual void prepare(::io_uring_sqe&) noexcept = 0;
};

static_assert(sizeof(std::atomic<unsigned>) == sizeof(unsigned));

struct params : ::io_uring_params {
  explicit params(::io_uring_params params) noexcept
    : ::io_uring_params(std::move(params))
  {}
  constexpr bool has_feature(const std::uint32_t feature) const noexcept {
    return bool(features & feature);
  }
  void check_features() const {
    if (!has_feature(IORING_FEAT_NODROP)) {
      throw std::runtime_error(
        "Kernel does not support \"almost never dropping completion "
        "events,\" dropped completion events lead to violation of the "
        "receiver contract, please upgrade your kernel");
    }
  }
  constexpr bool supports_single_mmap() const noexcept {
    return has_feature(IORING_FEAT_SINGLE_MMAP);
  }
  constexpr bool supports_completion_queue_entry_skip() const noexcept {
    return has_feature(IORING_FEAT_CQE_SKIP);
  }
  constexpr std::size_t submission_queue_size() const noexcept {
    return sq_off.array + (sq_entries * sizeof(unsigned));
  }
  constexpr std::size_t completion_queue_size() const noexcept {
    //  TODO: Big CQEs?
    return cq_off.cqes + (cq_entries * sizeof(::io_uring_cqe));
  }
  constexpr std::size_t submission_queue_entries_size() const noexcept {
    //  TODO: Big SQEs?
    return sq_entries * sizeof(::io_uring_sqe);
  }
};

template<typename T>
class queue {
  static constexpr void* get_(void* ptr, const std::uint32_t offset) noexcept {
    return static_cast<std::byte*>(ptr) + offset;
  }
  template<typename U>
  static constexpr U& get_as_(void* ptr, const std::uint32_t offset) noexcept {
    return *std::launder(
      reinterpret_cast<U*>(
        get_(ptr, offset)));
  }
public:
  constexpr unsigned to_index(const unsigned offset) const noexcept {
    return offset & ring_mask;
  }
  explicit constexpr queue(
    void* storage,
    const std::uint32_t head_offset,
    const std::uint32_t tail_offset,
    const std::uint32_t mask_offset,
    const std::uint32_t array_offset) noexcept
    : head(get_as_<std::atomic<unsigned>>(storage, head_offset)),
      tail(get_as_<std::atomic<unsigned>>(storage, tail_offset)),
      ring_mask(get_as_<unsigned>(storage, mask_offset)),
      array(static_cast<T*>(get_(storage, array_offset)))
  {}
  std::atomic<unsigned>& head;
  std::atomic<unsigned>& tail;
  unsigned ring_mask;
  T* array;
  constexpr bool empty() const noexcept {
    return
      head.load(std::memory_order_relaxed) ==
      tail.load(std::memory_order_relaxed);
  }
  constexpr T* get_from_tail(
    std::memory_order order = std::memory_order_acquire) noexcept
  {
    //  We're the only ones who update this, relaxed is fine
    const auto tail = this->tail.load(std::memory_order_relaxed);
    //  Kernel updates this, so we need acquire to synchronize
    const auto head = this->head.load(order);
    const auto u = to_index(tail);
    if (head != tail) {
      if (to_index(head) == u) {
        //  Ring is full, can't submit
        return nullptr;
      }
    }
    //  Good to use array element, we'll publish the fact that we've used it in
    //  the complete side of the operation
    return array + u;
  }
  constexpr void advance_tail() noexcept {
    //  We don't need a read memory ordering because only we update this, we
    //  need release because we're publishing this to the kernel
    tail.fetch_add(1, std::memory_order_release);
  }
  constexpr T* get_from_head() noexcept {
    const auto head = this->head.load(std::memory_order_relaxed);
    const auto tail = this->tail.load(std::memory_order_acquire);
    if (head == tail) {
      //  Empty
      return nullptr;
    }
    return array + to_index(head);
  }
  constexpr void advance_head() noexcept {
    head.fetch_add(1, std::memory_order_release);
  }
};

template<typename F, typename... Args>
  requires std::is_invocable_r_v<bool, F, Args...>
constexpr bool bool_invoke(F&& f, Args&&... args) noexcept(
  std::is_nothrow_invocable_r_v<F, Args...>)
{
  return bool(std::invoke(std::forward<F>(f), std::forward<Args>(args)...));
}

template<typename F, typename... Args>
  requires std::is_same_v<
    std::invoke_result_t<F, Args...>,
    void>
constexpr bool bool_invoke(F&& f, Args&&... args) noexcept(
  std::is_nothrow_invocable_v<F, Args...>)
{
  std::invoke(std::forward<F>(f), std::forward<Args>(args)...);
  return true;
}

class base {
protected:
  params params_;
  exec::safe_file_descriptor fd_;
  exec::memory_mapped_region submission_queue_map_;
  exec::memory_mapped_region completion_queue_map_;
  exec::memory_mapped_region submission_queue_entries_map_;
  queue<unsigned> submission_queue_;
  queue<::io_uring_cqe> completion_queue_;
  std::atomic<unsigned>& submission_queue_flags_;
  ::io_uring_sqe& get_sqe_(const unsigned u) noexcept {
    const auto ptr = static_cast<::io_uring_sqe*>(
      submission_queue_entries_map_.data());
    //  TODO: Account for big SQEs?
    return ptr[u];
  }
public:
  explicit base(const std::uint32_t entries, const ::io_uring_params& params)
    : params_(params),
      fd_([&]() {
        const auto retr = ::syscall(
          SYS_io_uring_setup,
          entries,
          &params_);
        if (retr < 0) {
          throw exception("syscall(__NR_io_uring_setup, ...)");
        }
        return int(retr);
      }()),
      submission_queue_map_([&]() {
        params_.check_features();
        std::size_t size = params_.submission_queue_size();
        if (params_.supports_single_mmap()) {
          size = std::max(size, params_.completion_queue_size());
        }
        const auto retr = ::mmap(
          0,
          size,
          PROT_READ | PROT_WRITE,
          MAP_SHARED | MAP_POPULATE,
          fd_.native_handle(),
          IORING_OFF_SQ_RING);
        if (retr == MAP_FAILED) {
          throw exception("mmap(..., IORING_OFF_SQ_RING)");
        }
        return exec::memory_mapped_region(retr, size);
      }()),
      completion_queue_map_([&]() {
        if (params_.supports_single_mmap()) {
          return exec::memory_mapped_region();
        }
        const auto size = params_.completion_queue_size();
        const auto retr = ::mmap(
          0,
          size,
          PROT_READ | PROT_WRITE,
          MAP_SHARED | MAP_POPULATE,
          fd_.native_handle(),
          IORING_OFF_CQ_RING);
        if (retr == MAP_FAILED) {
          throw exception("mmap(..., IORING_OFF_CQ_RING)");
        }
        return exec::memory_mapped_region(retr, size);
      }()),
      submission_queue_entries_map_([&]() {
        const auto size = params_.submission_queue_entries_size();
        const auto retr = ::mmap(
          0,
          size,
          PROT_READ | PROT_WRITE,
          MAP_SHARED | MAP_POPULATE,
          fd_.native_handle(),
          IORING_OFF_SQES);
        if (retr == MAP_FAILED) {
          throw exception("mmap(..., IORING_OFF_SQES)");
        }
        return exec::memory_mapped_region(retr, size);
      }()),
      submission_queue_(
        submission_queue_map_.data(),
        params_.sq_off.head,
        params_.sq_off.tail,
        params_.sq_off.ring_mask,
        params_.sq_off.array),
      completion_queue_(
        (completion_queue_map_ ? completion_queue_map_ : submission_queue_map_)
          .data(),
        params_.cq_off.head,
        params_.cq_off.tail,
        params_.cq_off.ring_mask,
        params_.cq_off.cqes),
      submission_queue_flags_([&]() noexcept -> std::atomic<unsigned>& {
        const auto ptr = static_cast<std::byte*>(submission_queue_map_.data());
        return *std::launder(
          reinterpret_cast<std::atomic<unsigned>*>(
            ptr + params_.sq_off.flags));
      }())
  {}
  bool need_wakeup() const noexcept {
    const auto flags = submission_queue_flags_.load(std::memory_order_relaxed);
    return bool(flags & IORING_SQ_NEED_WAKEUP);
  }
  bool submission_queue_empty() const noexcept {
    return submission_queue_.empty();
  }
  bool can_get_sqe() noexcept {
    return bool(submission_queue_.get_from_tail(std::memory_order_relaxed));
  }
  ::io_uring_sqe* get_sqe() noexcept {
    const auto ptr = submission_queue_.get_from_tail();
    if (!ptr) {
      return nullptr;
    }
    const std::size_t offset = ptr - submission_queue_.array;
    *ptr = offset;
    return &get_sqe_(offset);
  }
  void consume_sqe() noexcept {
    submission_queue_.advance_tail();
  }
  template<std::invocable<::io_uring_sqe&> Invocable>
  bool try_submit(Invocable i) noexcept(
    std::is_nothrow_invocable_v<Invocable, ::io_uring_sqe&>)
  {
    const auto ptr = get_sqe();
    if (!ptr) {
      return false;
    }
    if (!io_uring_context::bool_invoke(std::move(i), *ptr)) {
      //  True here because we did our job and invoked the invocable, i.e. there
      //  were SQEs the invocable just declined to consume any of them
      return true;
    }
    consume_sqe();
    return true;
  }
  template<std::invocable<const ::io_uring_cqe&> Invocable>
  bool try_complete(Invocable i) noexcept(
    std::is_nothrow_invocable_v<Invocable, const ::io_uring_cqe&>)
  {
    const auto ptr = completion_queue_.get_from_head();
    if (!ptr) {
      return false;
    }
    if (!io_uring_context::bool_invoke(std::move(i), *ptr)) {
      return true;
    }
    completion_queue_.advance_head();
    return true;
  }
  int enter(
    const unsigned to_submit,
    const unsigned min_complete,
    const unsigned flags,
    const void* const arg,
    const std::size_t argsz,
    std::error_code& ec) noexcept
  {
    ec.clear();
    const auto res = ::syscall(
      SYS_io_uring_enter,
      fd_.native_handle(),
      to_submit,
      min_complete,
      flags,
      arg,
      argsz);
    if (res < 0) {
      ec = std::error_code(errno, std::generic_category());
    }
    return res;
  }
  //  TODO: Bulk submit?
  //  TODO: Bulk complete?
};

struct submittable {
  template<typename>
  friend struct with_submittable_queue;
  virtual void submit(::io_uring_sqe&) noexcept = 0;
private:
  std::atomic<submittable*> next_{nullptr};
};

struct completable {
  virtual void complete(const ::io_uring_cqe&) noexcept = 0;
  auto user_data() noexcept {
    return reinterpret_cast<decltype(::io_uring_sqe::user_data)>(this);
  }
};

template<typename Base>
struct with_submittable_queue : Base {
  using Base::Base;
  //  From here until the next comment is the implementation of the mechanism
  //  which allows consumers to wait for an SQE to become available if one isn't
  //  eagerly available
  void enqueue(submittable& to_submit) noexcept {
    enqueue_(to_submit, to_submit);
  }
private:
  template<typename Receiver>
  class wait_for_sqe_operation_state_
    : public exec::inlinable_operation_state<
        wait_for_sqe_operation_state_<Receiver>,
        Receiver>,
      submittable
  {
  private:
    using base_ = exec::inlinable_operation_state<
      wait_for_sqe_operation_state_,
      Receiver>;
    virtual void submit(::io_uring_sqe& sqe) noexcept {
      ::stdexec::set_value(std::move(this->get_receiver()), sqe);
    }
  protected:
    with_submittable_queue& self_;
  public:
    constexpr wait_for_sqe_operation_state_(
      with_submittable_queue& self,
      Receiver r) noexcept
      : base_(std::move(r)),
        self_(self)
    {}
    void start() & noexcept {
      self_.enqueue(*this);
    }
  };
  template<template<typename> typename OperationState>
  class sqe_sender_ {
    using completion_signatures_ = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(::io_uring_sqe&)>;
  public:
    using sender_concept = ::stdexec::sender_t;
    template<typename Env>
    consteval completion_signatures_ get_completion_signatures(const Env&)
      noexcept
    {
      return {};
    }
    template<::stdexec::receiver_of<completion_signatures_> Receiver>
    constexpr OperationState<Receiver> connect(Receiver r) const noexcept {
      return OperationState<Receiver>(self_, std::move(r));
    }
    with_submittable_queue& self_;
  };
public:
  //  This is thread safe but always enqueues
  sqe_sender_<wait_for_sqe_operation_state_> wait_for_sqe() noexcept {
    return {*this};
  }
private:
  template<typename Receiver>
  class get_or_wait_for_sqe_operation_state_
    : wait_for_sqe_operation_state_<Receiver>
  {
    using base_ = wait_for_sqe_operation_state_<Receiver>;
  public:
    using base_::base_;
    void start() & noexcept {
      if (const auto sqe = this->self_.get_sqe(); sqe) {
        ::stdexec::set_value(std::move(this->get_receiver()), *sqe);
        return;
      }
      base_::start();
    }
  };
public:
  //  This is not thread safe but tries to get an SQE eagerly
  sqe_sender_<get_or_wait_for_sqe_operation_state_> get_or_wait_for_sqe()
    noexcept
  {
    return {*this};
  }
  void dequeue() noexcept {
    //  These leading two if checks make sure that we don't grab the entire
    //  queue, fail to submit anything, and then walk the entire queue to re-add
    //  it in a hot loop
    if (!head_.load(std::memory_order_relaxed)) {
      return;
    }
    //  Check to see if there are SQEs available
    if (!Base::can_get_sqe()) {
      //  If not we can't do anything
      return;
    }
    //  Now that we know we'll do some work we actually service the queue
    auto ptr = head_.exchange(nullptr, std::memory_order_acquire);
    while (ptr) {
      const auto current = ptr;
      ptr = ptr->next_.load(std::memory_order_relaxed);
      const auto sqe = Base::get_sqe();
      if (sqe) {
        current->next_.store(nullptr, std::memory_order_relaxed);
        current->submit(*sqe);
        continue;
      }
      if (!ptr) {
        enqueue_(*current, *current);
        break;
      }
      for (;;) {
        const auto next = ptr->next_.load(std::memory_order_relaxed);
        if (!next) {
          break;
        }
        ptr = next;
      }
      assert(ptr);
      enqueue_(*current, *ptr);
      break;
    }
  }
private:
  void enqueue_(submittable& head, submittable& tail) noexcept {
    assert(!tail.next_.load(std::memory_order_relaxed));
    assert(
      (&head != &tail) ||
      !head.next_.load(std::memory_order_relaxed));
#ifndef NDEBUG
    if (&head != &tail) {
      auto ptr = &head;
      for (;;) {
        const auto next = ptr->next_.load(std::memory_order_relaxed);
        assert(next);
        if (next == &tail) {
          break;
        }
        ptr = next;
      }
    }
#endif
    auto expected = head_.load(std::memory_order_relaxed);
    const auto update = [&]() noexcept {
      tail.next_.store(expected, std::memory_order_relaxed);
    };
    update();
    while (!head_.compare_exchange_weak(
      expected,
      &head,
      std::memory_order_release))
    {
      update();
    }
  }
  std::atomic<submittable*> head_{nullptr};
  //  Now comes the scheduler interface
  template<typename Receiver>
  class schedule_operation_state_
    : public exec::inlinable_operation_state<
        schedule_operation_state_<Receiver>,
        Receiver>,
      submittable
  {
    using base_ = exec::inlinable_operation_state<
      schedule_operation_state_,
      Receiver>;
    with_submittable_queue& ctx_;
    virtual void submit(::io_uring_sqe&) noexcept override {
      ::stdexec::set_value(std::move(base_::get_receiver()));
    }
  public:
    explicit constexpr schedule_operation_state_(
      with_submittable_queue& ctx,
      Receiver r) noexcept
      : base_(std::move(r)),
        ctx_(ctx)
    {}
    void start() & noexcept {
      ctx_.enqueue(*this);
    }
  };
  struct scheduler_;
  class schedule_sender_ {
    using completion_signatures_ = ::stdexec::completion_signatures<
      ::stdexec::set_value_t()>;
    struct env_ {
      constexpr scheduler_ query(
        const ::stdexec::get_completion_scheduler_t<::stdexec::set_value_t>&)
        const noexcept
      {
        return scheduler_{ctx_};
      }
      with_submittable_queue& ctx_;
    };
  public:
    using sender_concept = ::stdexec::sender_t;
    template<typename Env>
    consteval completion_signatures_ get_completion_signatures(const Env&)
      const noexcept
    {
      return {};
    }
    template<::stdexec::receiver_of<completion_signatures_> Receiver>
    constexpr schedule_operation_state_<Receiver> connect(Receiver r) const
      noexcept
    {
      return schedule_operation_state_<Receiver>(
        ctx_,
        std::move(r));
    }
    constexpr env_ get_env() const noexcept {
      return env_{ctx_};
    }
    with_submittable_queue& ctx_;
  };
  struct scheduler_ {
    constexpr bool operator==(const scheduler_& rhs) const noexcept {
      return &ctx_ == &rhs.ctx_;
    }
    constexpr schedule_sender_ schedule() const noexcept {
      return {ctx_};
    }
    with_submittable_queue& ctx_;
  };
public:
  constexpr scheduler_ get_scheduler() noexcept {
    return {*this};
  }
  //  Utility to submit an operation and then do something when it completes
private:
  template<typename Receiver>
  class wait_for_completion_operation_state_
    : public exec::inlinable_operation_state<
        wait_for_completion_operation_state_<Receiver>,
        Receiver>,
      //  This is public to enable consumers to access the user_data pointer
      public completable
  {
    using base_ = exec::inlinable_operation_state<
      wait_for_completion_operation_state_,
      Receiver>;
    virtual void complete(const ::io_uring_cqe& cqe) noexcept {
      ::stdexec::set_value(std::move(this->get_receiver()), cqe);        
    }
    with_submittable_queue& ctx_;
  public:
    explicit wait_for_completion_operation_state_(
      with_submittable_queue& ctx,
      ::io_uring_sqe& sqe,
      Receiver r) noexcept
      : base_(std::move(r)),
        ctx_(ctx)
    {
      //  Now when the CQE is dequeued this operation state will receive the
      //  complete invocation
      sqe.user_data = this->user_data();
    }
    void start() & noexcept {
      //  This actually gets the operation started
      ctx_.consume_sqe();
    }
  };
  class wait_for_completion_sender_ {
    using completion_signatures_ = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(const ::io_uring_cqe&)>;
  public:
    using sender_concept = ::stdexec::sender_t;
    with_submittable_queue& ctx_;
    ::io_uring_sqe& sqe_;
    template<typename Env>
    consteval completion_signatures_ get_completion_signatures(const Env&) const
      noexcept
    {
      return {};
    }
    //  It's important that this is rvalue qualified because connecting mutates
    //  the SQE and is therefore consumptive
    template<::stdexec::receiver_of<completion_signatures_> Receiver>
    auto connect(Receiver r) && noexcept {
      return wait_for_completion_operation_state_<Receiver>(
        ctx_,
        sqe_,
        std::move(r));
    }
  };
public:
  constexpr wait_for_completion_sender_ wait_for_completion(::io_uring_sqe& sqe)
    noexcept
  {
    return {*this, sqe};
  }
};

//  TODO: Remove
template<typename Base>
struct with_scheduler : Base {
  using Base::Base;
};

template<typename Env>
concept unstoppable_env = ::stdexec::unstoppable_token<
  ::stdexec::stop_token_of_t<Env>>;

template<typename Receiver>
concept unstoppable_receiver =
  ::stdexec::receiver<Receiver> &&
  unstoppable_env<::stdexec::env_of_t<Receiver>>;

using io_value_completion = ::stdexec::set_value_t(const ::io_uring_cqe&);

template<typename Env>
using io_completion_signatures = std::conditional_t<
  unstoppable_env<Env>,
  ::stdexec::completion_signatures<
    io_value_completion>,
  ::stdexec::completion_signatures<
    io_value_completion,
    ::stdexec::set_stopped_t()>>;

template<typename Receiver>
concept io_receiver =
  ::stdexec::receiver_of<
    Receiver,
    ::stdexec::completion_signatures<io_value_completion>> &&
  (
    unstoppable_receiver<Receiver> ||
    ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures<
        ::stdexec::set_stopped_t()>>);

template<typename StopToken, typename Invocable>
class stop_callback {
  struct invocable_ {
    explicit constexpr invocable_(Invocable i, bool& b) noexcept(
      std::is_nothrow_move_constructible_v<Invocable>)
      : inner_(std::move(i)),
        b_(b)
    {}
    void operator()() && noexcept {
      static_assert(std::is_nothrow_invocable_v<Invocable>);
      std::invoke(std::move(inner_));
      assert(!b_);
      b_ = true;
    }
  private:
    Invocable inner_;
    bool& b_;
  };
  using callback_type_ = ::stdexec::stop_callback_for_t<
    StopToken,
    invocable_>;
  bool stopped_{false};
  std::optional<callback_type_> o_;
public:
  constexpr void acquire(StopToken token, Invocable i) noexcept(
    std::is_nothrow_constructible_v<
      invocable_,
      Invocable,
      bool&> &&
    std::is_nothrow_constructible_v<
      callback_type_,
      StopToken,
      callback_type_>)
  {
    assert(!o_);
    o_.emplace(
      std::move(token),
      invocable_(
        std::move(i),
        stopped_));
  }
  constexpr bool release() noexcept {
    o_.reset();
    return stopped_;
  }
  constexpr bool acquired() const noexcept {
    return bool(o_);
  }
};

template<typename StopToken, typename Invocable>
  requires ::stdexec::unstoppable_token<StopToken>
struct stop_callback<StopToken, Invocable> {};

template<typename Invocable>
concept io_prepare_invocable =
  std::is_move_constructible_v<Invocable> &&
  std::invocable<Invocable, ::io_uring_sqe&> &&
  std::is_nothrow_invocable_v<Invocable, ::io_uring_sqe&>;

template<typename Derived>
struct io_submit_base : submittable {
  virtual void submit(::io_uring_sqe& sqe) noexcept override final {
    static_cast<Derived&>(*this).submit_io(sqe);
  }
};

template<typename Derived>
struct io_complete_base : completable {
  virtual void complete(const ::io_uring_cqe& cqe) noexcept override final {
    static_cast<Derived&>(*this).complete_io(cqe);
  }
};

template<typename Derived, bool>
struct io_submit_stop_base : submittable {
  virtual void submit(::io_uring_sqe& sqe) noexcept override final {
    static_cast<Derived&>(*this).submit_stop(sqe);
  }
};
template<typename Derived>
struct io_submit_stop_base<Derived, true> {};

template<typename Derived, bool>
struct io_complete_stop_base : completable {
  virtual void complete(const ::io_uring_cqe& cqe) noexcept override final {
    static_cast<Derived&>(*this).complete_stop(cqe);
  }
};
template<typename Derived>
struct io_complete_stop_base<Derived, true> {};

template<
  typename Context,
  io_prepare_invocable Invocable,
  io_receiver Receiver>
class io_operation_state
  : public exec::inlinable_operation_state<
      io_operation_state<Context, Invocable, Receiver>,
      Receiver>,
    io_submit_base<io_operation_state<Context, Invocable, Receiver>>,
    io_submit_stop_base<
      io_operation_state<Context, Invocable, Receiver>,
      unstoppable_receiver<Receiver>>,
    io_complete_base<io_operation_state<Context, Invocable, Receiver>>,
    io_complete_stop_base<
      io_operation_state<Context, Invocable, Receiver>,
      unstoppable_receiver<Receiver>>
{
  static constexpr bool unstoppable_ = unstoppable_receiver<Receiver>;
  using base_ = exec::inlinable_operation_state<io_operation_state, Receiver>;
  using submit_base_ = io_submit_base<io_operation_state>;
  using submit_stop_base_ = io_submit_stop_base<
    io_operation_state,
    unstoppable_>;
  using complete_base_ = io_complete_base<io_operation_state>;
  using complete_stop_base_ = io_complete_stop_base<
    io_operation_state,
    unstoppable_>;
  using base_::get_receiver;
  friend submit_base_;
  friend submit_stop_base_;
  friend complete_base_;
  friend complete_stop_base_;
  struct on_stop_request_ {
    explicit constexpr on_stop_request_(io_operation_state& self) noexcept
      : self_(self)
    {}
    void operator()() && noexcept {
      submit_stop_base_& self = self_;
      self_.ctx_.enqueue(self);
    }
  private:
    io_operation_state& self_;
  };
  Context& ctx_;
  [[no_unique_address]]
  Invocable i_;
  [[no_unique_address]]
  stop_callback<
    ::stdexec::stop_token_of_t<
      ::stdexec::env_of_t<Receiver>>,
    on_stop_request_> stop_;
  constexpr void start_(::io_uring_sqe& sqe) noexcept {
    if constexpr (!unstoppable_) {
      //  TODO!
      //static_assert(
      //  noexcept(
      //    stop_.acquire(
      //      ::stdexec::get_stop_token(
      //        ::stdexec::get_env(
      //          get_receiver())),
      //      on_stop_request_(*this))));
      stop_.acquire(
        ::stdexec::get_stop_token(
          ::stdexec::get_env(
            get_receiver())),
        on_stop_request_(*this));
    }
    std::invoke(std::move(i_), sqe);
    static_assert(sizeof(this) == sizeof(sqe.user_data));
    complete_base_* base = this;
    completable* self = base;
    sqe.user_data = reinterpret_cast<decltype(sqe.user_data)>(self);
  }
  constexpr void submit_io(::io_uring_sqe& sqe) noexcept {
    start_(sqe);
    ctx_.consume_sqe();
  }
  constexpr void submit_stop(::io_uring_sqe& sqe) noexcept
    requires (!unstoppable_)
  {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_ASYNC_CANCEL;
    {
      complete_stop_base_* base = this;
      completable* self = base;
      sqe.user_data = reinterpret_cast<decltype(sqe.user_data)>(self);
    }
    {
      complete_base_* base = this;
      completable* self = base;
      sqe.addr = reinterpret_cast<decltype(sqe.addr)>(self);
    }
    ctx_.consume_sqe();
  }
  constexpr void complete_io(const ::io_uring_cqe& cqe) noexcept {
    if constexpr (!unstoppable_) {
      //  Can't actually send the stop if a stop is requested and we're the
      //  first one in because the cancel operation is still outstanding
      const auto can_stop = !stop_.acquired();
      if (stop_.release()) {
        //  A stop was actually requested, send it if able
        if (can_stop) {
          ::stdexec::set_stopped(std::move(get_receiver()));
        }
        return;
      }
    }
    ::stdexec::set_value(std::move(get_receiver()), cqe);
  }
  void complete_stop(const ::io_uring_cqe& cqe) noexcept
    requires (!unstoppable_)
  {
    (void)cqe;
    if (stop_.acquired()) {
      //  First one in, can't proceed, need to wait for the actual I/O to
      //  complete
      (void)stop_.release();
      return;
    }
    ::stdexec::set_stopped(std::move(get_receiver()));
  }
public:
  explicit constexpr io_operation_state(
    Context& ctx,
    Invocable i,
    Receiver r) noexcept(
      std::is_nothrow_move_constructible_v<Invocable>)
    : base_(std::move(r)),
      ctx_(ctx),
      i_(std::move(i))
  {}
  void start() & noexcept {
    if (!ctx_.try_submit([&](::io_uring_sqe& sqe) noexcept {
      start_(sqe);
    })) {
      submit_base_& self = *this;
      ctx_.enqueue(self);
    }
  }
};

template<typename Context, io_prepare_invocable Invocable>
class io_sender {
  Context& ctx_;
  Invocable i_;
  template<typename Receiver>
  using operation_state_ = io_operation_state<
    Context,
    Invocable,
    Receiver>;
  static constexpr bool lvalue_connectable_ =
    std::is_copy_constructible_v<Invocable>;
public:
  using sender_concept = ::stdexec::sender_t;
  constexpr explicit io_sender(
    Context& ctx,
    Invocable i) noexcept(
      std::is_nothrow_move_constructible_v<Invocable>)
    : ctx_(ctx),
      i_(std::move(i))
  {}
  template<typename Env>
  consteval io_completion_signatures<Env> get_completion_signatures(const Env&)
    && noexcept
  {
    return {};
  }
  template<typename Env>
    requires lvalue_connectable_
  consteval io_completion_signatures<Env> get_completion_signatures(const Env&)
    const& noexcept
  {
    return {};
  }
  template<io_receiver Receiver>
    requires lvalue_connectable_
  constexpr operation_state_<Receiver> connect(Receiver r) const& noexcept(
    std::is_nothrow_constructible_v<
      operation_state_<Receiver>,
      Context&,
      const Invocable&,
      Receiver>)
  {
    return operation_state_<Receiver>(ctx_, i_, std::move(r));
  }
  template<io_receiver Receiver>
  constexpr operation_state_<Receiver> connect(Receiver r) && noexcept(
    std::is_nothrow_constructible_v<
      operation_state_<Receiver>,
      Context&,
      Invocable,
      Receiver>)
  {
    return operation_state_<Receiver>(ctx_, std::move(i_), std::move(r));
  }
};

template<typename Base>
struct with_io : Base {
  using Base::Base;
  template<io_prepare_invocable Invocable>
  io_sender<with_io, Invocable> io(Invocable i) noexcept(
    std::is_nothrow_move_constructible_v<Invocable>)
  {
    return io_sender<with_io, Invocable>(
      *this,
      std::move(i));
  }
};

using context = with_io<
  with_scheduler<
    with_submittable_queue<
      base>>>;

template<typename Context>
void complete(Context& ctx) noexcept {
  while (ctx.try_complete([](const ::io_uring_cqe& cqe) noexcept {
    assert(cqe.user_data);
    const auto ptr = reinterpret_cast<completable*>(cqe.user_data);
    ptr->complete(cqe);
  }));
}

template<typename Context>
void poll(Context& ctx, const std::atomic<bool>& done) noexcept {
  while (!done.load(std::memory_order_acquire)) {
    ctx.dequeue();
    io_uring_context::complete(ctx);
    if (ctx.need_wakeup() && !ctx.submission_queue_empty()) {
      std::error_code ec;
      (void)ctx.enter(
        0,
        0,
        IORING_ENTER_SQ_WAKEUP,
        nullptr,
        0,
        ec);
      //  TODO
      (void)ec;
      assert(!ec);
    }
  }
}

struct poll_runner {
  explicit poll_runner(
    const std::uint32_t entries,
    const ::io_uring_params& params)
    : ctx_(entries, params)
  {}
  context& get() & noexcept {
    return ctx_;
  }
  void run() & noexcept {
    poll(ctx_, done_);
  }
  void done() & noexcept {
    assert(!done_.load(std::memory_order_relaxed));
    done_.store(true, std::memory_order_release);
  }
private:
  context ctx_;
  std::atomic<bool> done_{false};
};

struct tag {};

template<typename T>
struct maybe_decay {
  using type = std::decay_t<T>;
};
template<typename T>
struct maybe_decay<T&> {
  using type = T&;
};

template<typename>
struct tuple;
template<typename Tag, typename... Args>
struct tuple<Tag(Args...)> {
  using type = std::tuple<Tag, typename maybe_decay<Args>::type...>;
};

template<typename>
struct storage;
template<typename... Signatures>
struct storage<::stdexec::completion_signatures<Signatures...>> {
  template<typename Tag, typename... Args>
  constexpr void arrive(Tag t, Args&&... args) noexcept(
    (std::is_nothrow_constructible_v<
      typename maybe_decay<Args>::type,
      Args> && ...))
  {
    assert(std::holds_alternative<std::monostate>(variant_));
    variant_.template emplace<
      typename tuple<Tag(Args...)>::type>(
        std::move(t),
        std::forward<Args>(args)...);
  }
  template<typename Visitor>
  constexpr void visit(Visitor&& v) && noexcept {
    assert(!std::holds_alternative<std::monostate>(variant_));
    std::visit([&](auto&& tuple) noexcept {
      if constexpr (std::is_same_v<
        std::monostate,
        std::remove_cvref_t<decltype(tuple)>>)
      {
        //  Unreachable
      } else {
        std::apply([&](auto&&... args) noexcept {
          static_assert(
            std::is_nothrow_invocable_v<
              Visitor,
              decltype(args)...>);
          std::invoke(
            std::forward<Visitor>(v),
            std::forward<decltype(args)>(args)...);
        }, std::forward<decltype(tuple)>(tuple));
      }
    }, std::move(variant_));
  }
private:
  std::variant<
    std::monostate,
    typename tuple<Signatures>::type...> variant_;
};

template<typename>
struct nothrow_signature;
template<typename Tag, typename... Args>
struct nothrow_signature<Tag(Args...)> {
  static constexpr bool value =
    (std::is_nothrow_constructible_v<
      typename maybe_decay<Args>::type,
      Args> && ...);
};

template<typename>
struct maybe_set_error_exception_ptr;
template<typename... Signatures>
struct maybe_set_error_exception_ptr<
  ::stdexec::completion_signatures<Signatures...>>
{
  using type = std::conditional_t<
    (nothrow_signature<Signatures>::value && ...),
    ::stdexec::completion_signatures<>,
    ::stdexec::completion_signatures<
      ::stdexec::set_error_t(std::exception_ptr)>>;
};

template<typename Runner, typename Sender, typename Env>
using run_completion_signatures = ::stdexec::transform_completion_signatures<
  ::stdexec::completion_signatures_of_t<Sender, Env>,
  //  TODO: What if running the runner throws?
  typename maybe_set_error_exception_ptr<
    ::stdexec::completion_signatures_of_t<Sender, Env>>::type>;

//  TODO: Should we use the environment to provide the reference to the context?
template<typename Runner, typename Sender, typename Receiver>
  requires ::stdexec::sender_to<Sender, Receiver>
class run_operation_state
  : Runner,
    public inlinable_operation_state<
      run_operation_state<Runner, Sender, Receiver>,
      Receiver>,
    public child_operation_state<
      run_operation_state<Runner, Sender, Receiver>,
      tag,
      ::stdexec::env_of_t<Receiver>,
      Sender>
{
  using base_ = inlinable_operation_state<run_operation_state, Receiver>;
  using base_::get_receiver;
  using env_ = ::stdexec::env_of_t<Receiver>;
  using operation_state_base_ = child_operation_state<
    run_operation_state,
    tag,
    env_,
    Sender>;
  using context_ = decltype(std::declval<Runner&>().get());
  using completion_signatures_ = run_completion_signatures<
    Runner,
    Sender,
    env_>;
  storage<completion_signatures_> result_;
public:
  template<typename Invocable, typename... Args>
    requires
      std::is_invocable_r_v<
        Sender,
        Invocable,
        context_> &&
      std::is_constructible_v<Runner, Args...>
  explicit constexpr run_operation_state(
    Invocable&& i,
    Receiver r,
    Args&&... args) noexcept(
      std::is_nothrow_constructible_v<Runner, Args...> &&
      std::is_nothrow_constructible_v<
        operation_state_base_,
        Sender> &&
      std::is_nothrow_invocable_r_v<
        Sender,
        Invocable,
        context_>)
      : Runner(std::forward<Args>(args)...),
        base_(std::move(r)),
        operation_state_base_(
          Sender(
            std::invoke(
              std::forward<Invocable>(i),
              Runner::get())))
  {}
  constexpr void start() & noexcept {
    operation_state_base_::start();
    Runner::run();
    std::move(result_).visit([&](const auto tag, auto&&... args) noexcept {
      tag(
        std::move(get_receiver()),
        std::forward<decltype(args)>(args)...);
    });
  }
  template<typename... Args>
  constexpr void set_value(const tag&, Args&&... args) noexcept {
    result_.arrive(::stdexec::set_value, std::forward<Args>(args)...);
    Runner::done();
  }
  template<typename... Args>
  constexpr void set_error(const tag&, Args&&... args) noexcept {
    result_.arrive(::stdexec::set_error, std::forward<Args>(args)...);
    Runner::done();
  }
  template<typename... Args>
  constexpr void set_stopped(const tag&, Args&&... args) noexcept {
    result_.arrive(::stdexec::set_stopped, std::forward<Args>(args)...);
    Runner::done();
  }
  constexpr env_ get_env(const tag&) noexcept {
    return ::stdexec::get_env(get_receiver());
  }
};

template<typename Runner, typename Invocable, typename... Args>
  requires
    std::is_move_constructible_v<Invocable> &&
    std::is_constructible_v<Runner, Args...>
class run_sender {
  Invocable i_;
  std::tuple<Args...> args_;
  using context_ = decltype(std::declval<Runner&>().get());
  template<typename ActualInvocable>
  using sender_ = std::invoke_result_t<
    ActualInvocable,
    context&>;
  template<typename ActualInvocable, typename Receiver>
  using operation_state_ = run_operation_state<
    Runner,
    sender_<ActualInvocable>,
    Receiver>;
  template<typename ActualInvocable, typename Receiver>
  static constexpr bool nothrow_connectable_ = std::is_nothrow_constructible_v<
    operation_state_<ActualInvocable, Receiver>,
    ActualInvocable,
    Receiver,
    std::conditional_t<
      std::is_const_v<std::remove_reference_t<ActualInvocable>>,
      const Args&,
      Args>...>;
  template<typename ActualInvocable, typename Env>
  using completion_signatures_ = run_completion_signatures<
    Runner,
    sender_<ActualInvocable>,
    Env>;
public:
  using sender_concept = ::stdexec::sender_t;
  template<typename... Ts>
    requires (std::is_constructible_v<Args, Ts> && ...)
  explicit constexpr run_sender(Invocable i, Ts&&... ts) noexcept(
    std::is_nothrow_move_constructible_v<Invocable> &&
    (std::is_nothrow_constructible_v<Args, Ts> && ...))
    : i_(std::move(i)),
      args_(std::forward<Ts>(ts)...)
  {}
  template<typename Env>
  consteval ::stdexec::completion_signatures_of_t<sender_<Invocable>, Env>
    get_completion_signatures(const Env&) && noexcept
  {
    return {};
  }
  template<typename Env>
  consteval ::stdexec::completion_signatures_of_t<
    sender_<const Invocable&>,
    Env> get_completion_signatures(const Env&) const& noexcept
  {
    return {};
  }
  template<typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        run_sender,
        ::stdexec::env_of_t<Receiver>>>
  constexpr operation_state_<Invocable, Receiver> connect(Receiver r) &&
    noexcept(nothrow_connectable_<Invocable, Receiver>)
  {
    return std::apply([&](auto&&... args) noexcept(
      nothrow_connectable_<Invocable, Receiver>)
    {
      return operation_state_<Invocable, Receiver>(
        std::move(i_),
        std::move(r),
        std::forward<decltype(args)>(args)...);
    }, std::move(args_));
  }
  template<typename Receiver>
    requires ::stdexec::receiver_of<
      Receiver,
      ::stdexec::completion_signatures_of_t<
        const run_sender&,
        ::stdexec::env_of_t<Receiver>>>
  constexpr operation_state_<const Invocable&, Receiver> connect(Receiver r)
    const& noexcept(nothrow_connectable_<const Invocable&, Receiver>)
  {
    return std::apply([&](auto&&... args) noexcept(
      nothrow_connectable_<const Invocable&, Receiver>)
    {
      return operation_state_<const Invocable&, Receiver>(
        i_,
        std::move(r),
        args...);
    }, std::move(args_));
  }
};

template<typename Invocable>
using run_on_polled_io_uring_sender = run_sender<
  poll_runner,
  Invocable,
  std::uint32_t,
  ::io_uring_params>;

}

using io_uring_context = detail::io_uring_context::context;

template<typename Invocable>
constexpr detail::io_uring_context::run_on_polled_io_uring_sender<Invocable>
  run_on_polled_io_uring(
    Invocable i,
    std::uint32_t entries,
    ::io_uring_params params) noexcept(
      std::is_nothrow_constructible_v<
        detail::io_uring_context::run_on_polled_io_uring_sender<Invocable>,
        Invocable,
        std::uint32_t,
        ::io_uring_params>)
{
  return detail::io_uring_context::run_on_polled_io_uring_sender<Invocable>(
    std::move(i),
    std::move(entries),
    std::move(params));
}

} // namespace exec

#endif   // if __has_include(<linux/io_uring.h>)
