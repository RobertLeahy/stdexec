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
#include <sstream>
#include <string>
#include <system_error>
#include <tuple>
#include <type_traits>
#include <utility>
#include <variant>
#include <errno.h>
#include <poll.h>
#include <string.h>
#include <sys/eventfd.h>
#include <sys/mman.h>
#include <sys/syscall.h>
#include <unistd.h>
#include "memory_mapped_region.hpp"
#include "safe_file_descriptor.hpp"
#include "../child_operation_state.hpp"
#include "../inlinable_operation_state.hpp"
#include "../storage_for_completion_signatures.hpp"
#include "../variant_child_operation_state.hpp"
#include "../../stdexec/execution.hpp"

namespace exec {

namespace detail::io_uring_context {

struct exception : std::system_error {
  explicit exception(const int err, std::string what)
    : std::system_error(
        std::error_code(
          err,
          std::system_category())),
      what_([&]() {
        std::ostringstream ss;
        ss << what
           << " failed: "
           << ::strerrordesc_np(err)
           << " ("
           << ::strerrorname_np(err)
           << " ("
           << code().value()
           << "))";
        return std::move(ss).str();
      }())
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
  constexpr bool full() const noexcept {
    const auto head = this->head.load(std::memory_order_relaxed);
    const auto tail = this->tail.load(std::memory_order_relaxed);
    if (head == tail) {
      //  In this case the ring is actually empty
      return false;
    }
    return to_index(head) == to_index(tail);
  }
  constexpr std::size_t size() const noexcept {
    return
      tail.load(std::memory_order_relaxed) -
      head.load(std::memory_order_relaxed);
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

struct completable {
  virtual void complete(const ::io_uring_cqe&) noexcept = 0;
  //  This is not const because it provides a way to call complete (above) which
  //  is not invocable on a const object
  auto user_data() noexcept {
    return reinterpret_cast<decltype(::io_uring_sqe::user_data)>(this);
  }
  void prepare_cancel(::io_uring_sqe& sqe) const noexcept {
    sqe = ::io_uring_sqe{}; //  constexpr std::memset to zero
    sqe.opcode = IORING_OP_ASYNC_CANCEL;
    sqe.addr = reinterpret_cast<decltype(sqe.addr)>(this);
  }
};

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
  const queue<unsigned>& submission_queue() const noexcept {
    return submission_queue_;
  }
  const queue<::io_uring_cqe>& completion_queue() const noexcept {
    return completion_queue_;
  }
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
    base& ctx_;
  public:
    explicit wait_for_completion_operation_state_(
      base& ctx,
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
    base& ctx_;
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

struct submittable {
  friend struct with_submittable_queue;
  virtual void submit(::io_uring_sqe&) noexcept = 0;
private:
  std::atomic<submittable*> next_{nullptr};
};

template<typename Invocable>
concept io_prepare_invocable =
  std::is_nothrow_move_constructible_v<Invocable> &&
  std::is_nothrow_invocable_v<Invocable, ::io_uring_sqe&>;

struct with_submittable_queue : base, private completable {
  explicit with_submittable_queue(
    const std::uint32_t entries,
    const ::io_uring_params& params)
    : base(entries, params),
      eventfd_(::eventfd(0, EFD_CLOEXEC))
  {
    if (!eventfd_) {
      throw exception("eventfd");
    }
    const auto ptr = get_sqe();
    assert(ptr);
    std::memset(ptr, 0, sizeof(*ptr));
    ptr->opcode = IORING_OP_POLL_ADD;
    ptr->fd = eventfd_.native_handle();
    ptr->poll_events = POLLIN;
    ptr->len = IORING_POLL_ADD_MULTI;
    ptr->user_data = this->user_data();
    consume_sqe();
  }
  void enqueue(submittable& to_submit) noexcept {
    enqueue_(to_submit, to_submit);
  }
private:
  struct tag_ {};
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
  public:
    with_submittable_queue& ctx_;
    constexpr explicit wait_for_sqe_operation_state_(
      with_submittable_queue& ctx,
      Receiver r) noexcept
      : base_(std::move(r)),
        ctx_(ctx)
    {}
    void start() & noexcept {
      ctx_.enqueue(*this);
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
  using wait_for_sqe_sender_ = sqe_sender_<wait_for_sqe_operation_state_>;
public:
  //  This is thread safe but always enqueues
  wait_for_sqe_sender_ wait_for_sqe() noexcept {
    return {*this};
  }
private:
  template<typename Receiver>
  class get_or_wait_for_sqe_operation_state_ :
    public ::exec::inlinable_operation_state<
      get_or_wait_for_sqe_operation_state_<Receiver>,
      Receiver>,
    public ::exec::child_operation_state<
      get_or_wait_for_sqe_operation_state_<Receiver>,
      tag_,
      ::stdexec::env_of_t<Receiver>,
      wait_for_sqe_sender_>
  {
    using receiver_base_ = ::exec::inlinable_operation_state<
      get_or_wait_for_sqe_operation_state_,
      Receiver>;
    using base_ = ::exec::child_operation_state<
      get_or_wait_for_sqe_operation_state_,
      tag_,
      ::stdexec::env_of_t<Receiver>,
      wait_for_sqe_sender_>;
  public:
    constexpr explicit get_or_wait_for_sqe_operation_state_(
      with_submittable_queue& ctx,
      Receiver r) noexcept
      : receiver_base_(std::move(r)),
        base_(ctx.wait_for_sqe())
    {}
    void start() & noexcept {
      auto&& op = base_::get();
      if (const auto sqe = op.ctx_.get_sqe(); sqe) {
        ::stdexec::set_value(std::move(this->get_receiver()), *sqe);
        return;
      }
      base_::start();
    }
    constexpr void set_value(tag_, ::io_uring_sqe& sqe) noexcept {
      ::stdexec::set_value(std::move(this->get_receiver()), sqe);
    }
    constexpr decltype(auto) get_env(tag_) noexcept {
      return ::stdexec::get_env(this->get_receiver());
    }
  };
  using get_or_wait_for_sqe_sender_ =
    sqe_sender_<get_or_wait_for_sqe_operation_state_>;
public:
  //  This is not thread safe but tries to get an SQE eagerly
  get_or_wait_for_sqe_sender_ get_or_wait_for_sqe() noexcept {
    return {*this};
  }
  void dequeue() noexcept {
    if (!can_dequeue_()) {
      return;
    }
    //  Now that we know we'll do some work we actually service the queue
    auto ptr = head_.exchange(nullptr, std::memory_order_acquire);
    while (ptr) {
      const auto current = ptr;
      ptr = ptr->next_.load(std::memory_order_relaxed);
      const auto sqe = get_sqe();
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
  void wakeup() noexcept {
    if (needs_wakeup_.exchange(true, std::memory_order_acquire)) {
      //  We've already written to the eventfd, save a syscall
      return;
    }
    const std::uint64_t to_write = 1;
    //  Unfortunately this is a syscall because we don't know which thread we're
    //  on, and we're pretty sure we can't get a SQE
    const auto res = ::write(
      eventfd_.native_handle(),
      &to_write,
      sizeof(to_write));
    assert(res == sizeof(to_write));
    (void)res;
  }
private:
  //  This function is no longer used but is maintained anyway, it checks to see
  //  if dequeue() would do anything and so can be used to avoid the overhead of
  //  grabbing the queue, doing nothing, and having to walk the entire queue to
  //  re-add it
  //
  //  It's no longer used since with the eventfd we know we'll always do work
  //  when dequeue() is called
  //
  //  The function is not const because con_get_sqe() is not const
  constexpr bool can_dequeue_() noexcept {
    //  These leading two if checks make sure that we don't grab the entire
    //  queue, fail to submit anything, and then walk the entire queue to re-add
    //  it in a hot loop
    if (!head_.load(std::memory_order_relaxed)) {
      return false;
    }
    //  Check to see if there are SQEs available
    if (!can_get_sqe()) {
      //  If not we can't do anything
      return false;
    }
    return true;
  }
  virtual void complete(const ::io_uring_cqe& cqe) noexcept override {
    (void)cqe;
    assert(cqe.res >= 0);
    assert(cqe.flags & IORING_CQE_F_MORE);
    needs_wakeup_.store(false, std::memory_order_release);
    dequeue();
  }
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
    wakeup();
  }
  std::atomic<submittable*> head_{nullptr};
  std::atomic<bool> needs_wakeup_{false};
  exec::safe_file_descriptor eventfd_;
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
private:
  template<typename> struct unary_tag_ {};
  using wait_for_completion_sender_ = decltype(
    std::declval<base&>().wait_for_completion(
      std::declval<::io_uring_sqe&>()));
  template<typename Receiver>
  class cancel_operation_state_
    : public exec::inlinable_operation_state<
        cancel_operation_state_<Receiver>,
        Receiver>,
      public exec::variant_child_operation_state<
        cancel_operation_state_<Receiver>,
        unary_tag_,
        ::stdexec::env_of_t<Receiver>,
        wait_for_sqe_sender_,
        wait_for_completion_sender_>
  {
    using receiver_base_ = exec::inlinable_operation_state<
      cancel_operation_state_,
      Receiver>;
    using ops_base_ = exec::variant_child_operation_state<
      cancel_operation_state_,
      unary_tag_,
      ::stdexec::env_of_t<Receiver>,
      wait_for_sqe_sender_,
      wait_for_completion_sender_>;
    with_submittable_queue& ctx_;
    const completable* op_;
  public:
    explicit constexpr cancel_operation_state_(
      with_submittable_queue& ctx,
      const completable& op,
      Receiver r) noexcept
      : receiver_base_(std::move(r)),
        ctx_(ctx),
        op_(&op)
    {
      this->construct(ctx_.wait_for_sqe());
    }
    constexpr ~cancel_operation_state_() noexcept {
      if (op_) {
        //  Operation state was destroyed without starting, we need to destroy
        //  the child operation state
        this->template destruct<wait_for_sqe_sender_>();
      }
    }
    void start() & noexcept {
      ops_base_::template start<wait_for_sqe_sender_>();
    }
    void set_value(unary_tag_<wait_for_sqe_sender_>, ::io_uring_sqe& sqe)
      noexcept
    {
      assert(op_);
      op_->prepare_cancel(sqe);
      op_ = nullptr;
      this->template destruct<wait_for_sqe_sender_>();
      this->construct(ctx_.wait_for_completion(sqe));
      ops_base_::template start<wait_for_completion_sender_>();
    }
    void set_value(
      unary_tag_<wait_for_completion_sender_>,
      const ::io_uring_cqe& cqe) noexcept
    {
      (void)cqe;
      this->template destruct<wait_for_completion_sender_>();
      ::stdexec::set_value(std::move(this->get_receiver()));
    }
    template<typename Tag>
    constexpr decltype(auto) get_env(Tag) noexcept {
      return ::stdexec::get_env(this->get_receiver());
    }
  };
  class cancel_sender_ {
    using completion_signatures_ = ::stdexec::completion_signatures<
      ::stdexec::set_value_t()>;
  public:
    using sender_concept = ::stdexec::sender_t;
    template<typename Env>
    consteval completion_signatures_ get_completion_signatures(const Env&) const
      noexcept
    {
      return {};
    }
    template<::stdexec::receiver_of<completion_signatures_> Receiver>
    constexpr auto connect(Receiver r) && noexcept {
      return cancel_operation_state_<Receiver>(
        ctx_,
        op_,
        std::move(r));
    }
    with_submittable_queue& ctx_;
    const completable& op_;
  };
public:
  constexpr cancel_sender_ cancel(const completable& op) noexcept {
    return {*this, op};
  }
private:
  template<typename Prepare, typename Receiver>
  class io_operation_state_
    : public exec::inlinable_operation_state<
        io_operation_state_<Prepare, Receiver>,
        Receiver>,
      public exec::variant_child_operation_state<
        io_operation_state_<Prepare, Receiver>,
        unary_tag_,
        ::stdexec::env_of_t<Receiver>,
        get_or_wait_for_sqe_sender_,
        wait_for_completion_sender_>
  {
    using receiver_base_ = exec::inlinable_operation_state<
      io_operation_state_,
      Receiver>;
    using ops_base_ = exec::variant_child_operation_state<
      io_operation_state_,
      unary_tag_,
      ::stdexec::env_of_t<Receiver>,
      get_or_wait_for_sqe_sender_,
      wait_for_completion_sender_>;
    with_submittable_queue* ctx_;
    Prepare prepare_;
  public:
    explicit constexpr io_operation_state_(
      with_submittable_queue& ctx,
      Prepare prepare,
      Receiver r) noexcept
      : receiver_base_(std::move(r)),
        ctx_(&ctx),
        prepare_(std::move(prepare))
    {
      this->construct(ctx_->get_or_wait_for_sqe());
    }
    constexpr ~io_operation_state_() noexcept {
      if (ctx_) {
        //  Operation state was destroyed without starting, we need to destroy
        //  the child operation state
        this->template destruct<get_or_wait_for_sqe_sender_>();
      }
    }
    void start() & noexcept {
      ops_base_::template start<get_or_wait_for_sqe_sender_>();
    }
    void set_value(unary_tag_<get_or_wait_for_sqe_sender_>, ::io_uring_sqe& sqe)
      noexcept
    {
      this->template destruct<get_or_wait_for_sqe_sender_>();
      std::invoke(std::move(prepare_), sqe);
      assert(ctx_);
      this->construct(ctx_->wait_for_completion(sqe));
      ops_base_::template start<wait_for_completion_sender_>();
    }
    void set_value(
      unary_tag_<wait_for_completion_sender_>,
      const ::io_uring_cqe& cqe) noexcept
    {
      this->template destruct<wait_for_completion_sender_>();
      ::stdexec::set_value(std::move(this->get_receiver()), cqe);
    }
    template<typename Tag>
    constexpr decltype(auto) get_env(Tag) noexcept {
      return ::stdexec::get_env(this->get_receiver());
    }
  };
  template<typename Prepare>
  class io_sender_ {
    using completion_signatures_ = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(const ::io_uring_cqe&)>;
  public:
    using sender_concept = ::stdexec::sender_t;
    template<typename Env>
    consteval completion_signatures_ get_completion_signatures(const Env&) const
      noexcept
    {
      return {};
    }
    template<
      typename Self,
      ::stdexec::receiver_of<completion_signatures_> Receiver>
      requires std::is_constructible_v<
        Prepare,
        ::exec::like_t<Self, Prepare>>
    constexpr auto connect(this Self&& self, Receiver r) noexcept(
      std::is_nothrow_constructible_v<
        Prepare,
        ::exec::like_t<Self, Prepare>>)
    {
      return io_operation_state_<Prepare, Receiver>(
        self.ctx_,
        std::forward<Self>(self).prepare_,
        std::move(r));
    }
    with_submittable_queue& ctx_;
    Prepare prepare_;
  };
  template<typename Prepare, typename Receiver>
  class stoppable_io_operation_state_
    : public exec::inlinable_operation_state<
        stoppable_io_operation_state_<Prepare, Receiver>,
        Receiver>,
      public exec::variant_child_operation_state<
        stoppable_io_operation_state_<Prepare, Receiver>,
        unary_tag_,
        ::stdexec::env_of_t<Receiver>,
        get_or_wait_for_sqe_sender_,
        wait_for_completion_sender_>,
      public exec::manual_child_operation_state<
        stoppable_io_operation_state_<Prepare, Receiver>,
        tag_,
        ::stdexec::env_of_t<Receiver>,
        cancel_sender_>
  {
    using receiver_base_ = exec::inlinable_operation_state<
      stoppable_io_operation_state_,
      Receiver>;
    using ops_base_ = exec::variant_child_operation_state<
      stoppable_io_operation_state_,
      unary_tag_,
      ::stdexec::env_of_t<Receiver>,
      get_or_wait_for_sqe_sender_,
      wait_for_completion_sender_>;
    using cancel_base_ = exec::manual_child_operation_state<
      stoppable_io_operation_state_,
      tag_,
      ::stdexec::env_of_t<Receiver>,
      cancel_sender_>;
    struct on_stop_request_ {
      stoppable_io_operation_state_& self_;
      void operator()() && noexcept {
        self_.cancel_base_::construct(
          self_.ctx_.cancel(
            self_.ops_base_::template get<wait_for_completion_sender_>()));
        ++self_.outstanding;
        self_.cancel_base_::start();
      }
    };
    using stop_token_type_ = ::stdexec::stop_token_of_t<
      ::stdexec::env_of_t<Receiver>>;
    using stop_callback_type_ = ::stdexec::stop_callback_for_t<
      stop_token_type_,
      on_stop_request_>;
    with_submittable_queue& ctx_;
    std::variant<
      std::monostate,
      Prepare,
      stop_callback_type_> storage_;
    unsigned outstanding{1};
  public:
    explicit stoppable_io_operation_state_(
      with_submittable_queue& ctx,
      Prepare prepare,
      Receiver r) noexcept
      : receiver_base_(std::move(r)),
        ctx_(ctx),
        storage_(std::move(prepare))
    {
      ops_base_::construct(ctx_.get_or_wait_for_sqe());
    }
    constexpr ~stoppable_io_operation_state_() noexcept {
      if (std::holds_alternative<Prepare>(storage_)) {
        ops_base_::template destruct<get_or_wait_for_sqe_sender_>();
      }
    }
    void start() & noexcept {
      ops_base_::template start<get_or_wait_for_sqe_sender_>();
    }
    void set_value(unary_tag_<get_or_wait_for_sqe_sender_>, ::io_uring_sqe& sqe)
      noexcept
    {
      ops_base_::template destruct<get_or_wait_for_sqe_sender_>();
      const auto ptr = std::get_if<Prepare>(&storage_);
      assert(ptr);
      if (!ptr) {
        STDEXEC_UNREACHABLE();
      }
      std::invoke(std::move(*ptr), sqe);
      ops_base_::construct(ctx_.wait_for_completion(sqe));
      ops_base_::template start<wait_for_completion_sender_>();
      //  Normally there would be a concern that the operation state is
      //  potentially outside its lifetime however we know this isn't true
      //  because:
      //
      //  - We just got an SQE which always happens on the thread servicing the
      //    ring, and
      //  - Completion can't happen inline because we don't check the completion
      //    queue
      storage_.template emplace<stop_callback_type_>(
        ::stdexec::get_stop_token(::stdexec::get_env(this->get_receiver())),
        on_stop_request_{*this});
    }
    void set_value(
      unary_tag_<wait_for_completion_sender_>,
      const ::io_uring_cqe& cqe) noexcept
    {
      //  Once this returns the stop callback is destroyed and can no longer be
      //  invoked and therefore we don't have to worry about races therewith
      storage_.template emplace<std::monostate>();
      ops_base_::template destruct<wait_for_completion_sender_>();
      --outstanding;
      if (outstanding) {
        //  We'll let the stop operation take care of things since it's
        //  outstanding
        return;
      }
      if (
        ::stdexec::get_stop_token(
          ::stdexec::get_env(
            this->get_receiver())).stop_requested())
      {
        ::stdexec::set_stopped(std::move(this->get_receiver()));
        return;
      }
      ::stdexec::set_value(std::move(this->get_receiver()), cqe);
    }
    void set_value(tag_) noexcept {
      cancel_base_::destruct();
      --outstanding;
      if (outstanding) {
        //  We were first to finish, do nothing
        return;
      }
      ::stdexec::set_stopped(std::move(this->get_receiver()));
    }
    template<typename Tag>
    constexpr decltype(auto) get_env(Tag) noexcept {
      return ::stdexec::get_env(this->get_receiver());
    }
  };
  template<typename Prepare>
  class stoppable_io_sender_ {
    using unstoppable_completion_signatures_ = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(const ::io_uring_cqe&)>;
    using stoppable_completion_signatures_ = ::stdexec::completion_signatures<
      ::stdexec::set_value_t(const ::io_uring_cqe&),
      ::stdexec::set_stopped_t()>;
    template<typename Env>
    static constexpr bool unstoppable_ = ::stdexec::unstoppable_token<
      ::stdexec::stop_token_of_t<Env>>;
  public:
    using sender_concept = ::stdexec::sender_t;
    template<typename Env>
    consteval auto get_completion_signatures(const Env&) const noexcept {
      if constexpr (unstoppable_<Env>) {
        return unstoppable_completion_signatures_{};
      } else {
        return stoppable_completion_signatures_{};
      }
    }
    template<typename Self, typename Receiver>
      requires
        std::is_constructible_v<
          Prepare,
          ::exec::like_t<Self, Prepare>> &&
        ::stdexec::receiver_of<
          Receiver,
          ::stdexec::completion_signatures_of_t<
            Self,
            ::stdexec::env_of_t<Receiver>>>
    constexpr auto connect(this Self&& self, Receiver r) noexcept(
      std::is_nothrow_constructible_v<
        Prepare,
        ::exec::like_t<Self, Prepare>>)
    {
      if constexpr (unstoppable_<::stdexec::env_of_t<Receiver>>) {
        return io_operation_state_<Prepare, Receiver>(
          self.ctx_,
          std::forward<Self>(self).prepare_,
          std::move(r));
      } else {
        return stoppable_io_operation_state_<Prepare, Receiver>(
          self.ctx_,
          std::forward<Self>(self).prepare_,
          std::move(r));
      }
    }
    with_submittable_queue& ctx_;
    Prepare prepare_;
  };
public:
  template<typename Prepare>
  constexpr stoppable_io_sender_<Prepare> io(Prepare prepare) noexcept {
    return {*this, std::move(prepare)};
  }
};

using context = with_submittable_queue;

inline void complete(base& ctx) noexcept {
  while (ctx.try_complete([](const ::io_uring_cqe& cqe) noexcept {
    assert(cqe.user_data);
    const auto ptr = reinterpret_cast<completable*>(cqe.user_data);
    ptr->complete(cqe);
  }));
}

inline void poll(context& ctx, const std::atomic<bool>& done) noexcept {
  while (!done.load(std::memory_order_acquire)) {
    io_uring_context::complete(ctx);
    if (ctx.need_wakeup()) {
      std::error_code ec;
      (void)ctx.enter(
        0,
        0,
        IORING_ENTER_SQ_WAKEUP,
        nullptr,
        0,
        ec);
      //  Should we react to this somehow? We can't afford to stop making
      //  forward progress due to the receiver contract so we just assume this
      //  never happens
      (void)ec;
      assert(!ec);
    }
  }
}

inline void block(context& ctx, const std::atomic<bool>& done) noexcept {
  //  This structure ensures that done is reloaded before blocking after
  //  completing
  io_uring_context::complete(ctx);
  while (!done.load(std::memory_order_acquire)) {
    unsigned int flags = IORING_ENTER_GETEVENTS;
#ifdef IORING_ENTER_NO_IOWAIT
    flags |= IORING_ENTER_NO_IOWAIT;
#endif
    std::error_code ec;
    (void)ctx.enter(
      ctx.submission_queue().size(),
      1,
      flags,
      nullptr,
      0,
      ec);
    (void)ec;
    assert(!ec);
    io_uring_context::complete(ctx);
  }
}

struct poll_runner {
  explicit poll_runner(
    const std::uint32_t entries,
    const ::io_uring_params& params)
    : ctx_(
        entries,
        [&]() noexcept {
          auto retr = params;
          retr.flags |= IORING_SETUP_SQPOLL;
          retr.flags |= IORING_SETUP_SINGLE_ISSUER;
          return retr;
        }())
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

struct blocking_runner {
  explicit blocking_runner(
    const std::uint32_t entries,
    const ::io_uring_params& params)
    : ctx_(
        entries,
        [&]() noexcept {
          auto retr = params;
          retr.flags |= IORING_SETUP_SUBMIT_ALL;
          retr.flags |= IORING_SETUP_COOP_TASKRUN;
          retr.flags |= IORING_SETUP_SINGLE_ISSUER;
          return retr;
        }())
  {}
  context& get() & noexcept {
    return ctx_;
  }
  void run() & noexcept {
    block(ctx_, done_);
  }
  void done() & noexcept {
    assert(!done_.load(std::memory_order_relaxed));
    done_.store(true, std::memory_order_release);
    //  If this function is called from a different thread we need to make sure
    //  the main thread wakes up to check the atomic
    ctx_.wakeup();
  }
private:
  context ctx_;
  std::atomic<bool> done_{false};
};

struct tag {};

template<typename Sender, typename Env>
using run_storage_for_completion_signatures =
  ::exec::storage_for_completion_signatures<
    ::stdexec::completion_signatures_of_t<Sender, Env>>;

template<typename Runner>
inline constexpr bool is_nothrow_runner = noexcept(std::declval<Runner&>().run());

template<typename Runner, typename Sender, typename Env>
using run_completion_signatures = ::stdexec::transform_completion_signatures<
  typename run_storage_for_completion_signatures<Sender, Env>::completion_signatures,
  std::conditional_t<
    is_nothrow_runner<Runner>,
    ::stdexec::completion_signatures<>,
    ::stdexec::completion_signatures<::stdexec::set_error_t(std::exception_ptr)>>>;

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
  run_storage_for_completion_signatures<Sender, env_> result_;
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
    if constexpr (noexcept(Runner::run())) {
      Runner::run();
    } else {
      try {
        Runner::run();
      } catch (...) {
        ::stdexec::set_error(
          std::move(get_receiver()),
          std::current_exception());
        return;
      }
    }
    std::move(result_).complete(std::move(get_receiver()));
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
  consteval completion_signatures_<Invocable, Env> get_completion_signatures(
    const Env&) && noexcept
  {
    return {};
  }
  template<typename Env>
  consteval completion_signatures_<const Invocable&, Env>
    get_completion_signatures(const Env&) const& noexcept
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

template<typename Runner>
class run_on_io_uring_t {
  template<typename Invocable>
  using sender_ = run_sender<
    Runner,
    std::remove_cvref_t<Invocable>,
    std::uint32_t,
    ::io_uring_params>;
public:
  template<typename Invocable>
  constexpr auto operator()(
    Invocable&& i,
    const std::uint32_t entries,
    const ::io_uring_params& params) const noexcept(
      std::is_nothrow_constructible_v<
        sender_<Invocable>,
        Invocable,
        const std::uint32_t&,
        const ::io_uring_params&>)
  {
    return sender_<Invocable>(
      std::forward<Invocable>(i),
      entries,
      params);
  }
};

}

using io_uring_context = detail::io_uring_context::context;

inline constexpr detail::io_uring_context::run_on_io_uring_t<
  detail::io_uring_context::poll_runner> run_on_polled_io_uring{};

inline constexpr detail::io_uring_context::run_on_io_uring_t<
  detail::io_uring_context::blocking_runner> run_on_blocking_io_uring{};

} // namespace exec

#endif   // if __has_include(<linux/io_uring.h>)
