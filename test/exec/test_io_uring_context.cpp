#include <exec/linux/io_uring_context.hpp>

#include <linux/io_uring.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstring>
#include <exception>
#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>
#include <exec/finally.hpp>
#include <exec/linux/safe_file_descriptor.hpp>
#include <exec/env.hpp>
#include "../test_common/receivers.hpp"
#include "../test_common/type_helpers.hpp"

#include "catch2/catch.hpp"

namespace {

using namespace exec;

struct throwing_move {
  throwing_move(throwing_move&&);
};

static_assert(
  std::is_same_v<
    detail::io_uring_context::maybe_decay<int>::type,
    int>);
static_assert(
  std::is_same_v<
    detail::io_uring_context::maybe_decay<int&&>::type,
    int>);
static_assert(
  std::is_same_v<
    detail::io_uring_context::maybe_decay<int&>::type,
    int&>);
static_assert(
  std::is_same_v<
    detail::io_uring_context::maybe_decay<const int&>::type,
    const int&>);

static_assert(
  detail::io_uring_context::nothrow_signature<
    ::stdexec::set_value_t()>::value);
static_assert(
  detail::io_uring_context::nothrow_signature<
    ::stdexec::set_value_t(int)>::value);
static_assert(
  detail::io_uring_context::nothrow_signature<
    ::stdexec::set_value_t(int&&)>::value);
static_assert(
  detail::io_uring_context::nothrow_signature<
    ::stdexec::set_value_t(const int&)>::value);
static_assert(
  !detail::io_uring_context::nothrow_signature<
    ::stdexec::set_value_t(throwing_move)>::value);
static_assert(
  !detail::io_uring_context::nothrow_signature<
    ::stdexec::set_value_t(throwing_move&&)>::value);
static_assert(
  detail::io_uring_context::nothrow_signature<
    ::stdexec::set_value_t(const throwing_move&)>::value);
static_assert(
  detail::io_uring_context::nothrow_signature<
    ::stdexec::set_stopped_t()>::value);
static_assert(
  detail::io_uring_context::nothrow_signature<
    ::stdexec::set_error_t(std::exception_ptr)>::value);

template<typename Predicate>
void poll_until(detail::io_uring_context::base& base, Predicate pred) {
  const auto timeout =
    std::chrono::steady_clock::now() + std::chrono::seconds(1);
  while (!pred()) {
    if (base.need_wakeup()) {
      std::error_code ec;
      base.enter(
        0,
        0,
        IORING_ENTER_SQ_WAKEUP,
        nullptr,
        0,
        ec);
      REQUIRE(!ec);
    }
    REQUIRE(std::chrono::steady_clock::now() < timeout);
  }
}

TEST_CASE("A single IORING_OP_NOP is submitted and completed in submission "
  "queue polling mode", "[io_uring][io_uring_context]")
{
  detail::io_uring_context::base base(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  CHECK(base.try_submit([](::io_uring_sqe& sqe) noexcept {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_NOP;
  }));
  poll_until(
    base,
    [&]() {
      return base.try_complete([](const ::io_uring_cqe& cqe) {
        CHECK(cqe.res == 0);
      });
    });
}

TEST_CASE("In submission queue polling mode IORING_OP_NOPs may be submitted "
  "until the submission queue is full, thereafter they complete", "[io_uring][io_uring_context]")
{
  detail::io_uring_context::base base(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  std::size_t submitted = 0;
  while (base.try_submit([](::io_uring_sqe& sqe) noexcept {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_NOP;
  })) {
    ++submitted;
  }
  CHECK(submitted > 1);
  std::size_t completed = 0;
  poll_until(
    base,
    [&]() {
      if (!base.try_complete([](const ::io_uring_cqe& cqe) {
        CHECK(cqe.res == 0);
      })) {
        return false;
      }
      ++completed;
      return completed == submitted;
    });
  poll_until(
    base,
    [&]() noexcept {
      return !base.try_submit([](::io_uring_sqe& sqe) noexcept {
        std::memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_NOP;
      });
    });
  poll_until(
    base,
    [&]() {
      return !base.try_complete([](const ::io_uring_cqe& cqe) {
        CHECK(cqe.res == 0);
      });
    });
}

TEST_CASE("When the kernel thread goes to sleep it can be awoken with "
  "io_uring_enter", "[io_uring][io_uring_context]")
{
  detail::io_uring_context::base base(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 20;
      return retr;
    }());
  CHECK(!base.need_wakeup());
  const auto now = std::chrono::steady_clock::now();
  while (!base.need_wakeup()) {
    REQUIRE(std::chrono::steady_clock::now() < (now + std::chrono::seconds(1)));
  }
  std::error_code ec;
  const auto submitted = base.enter(
    0,
    0,
    IORING_ENTER_SQ_WAKEUP,
    nullptr,
    0,
    ec);
  while (!base.need_wakeup()) {
    REQUIRE(std::chrono::steady_clock::now() < (now + std::chrono::seconds(1)));
  }
  REQUIRE(!ec);
  CHECK(submitted == 0);
}

TEST_CASE("When the kernel thread goes to sleep it can be awoken with "
  "io_uring_enter while also submitting I/O", "[io_uring][io_uring_context]")
{
  detail::io_uring_context::base base(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 20;
      return retr;
    }());
  CHECK(!base.need_wakeup());
  const auto now = std::chrono::steady_clock::now();
  while (!base.need_wakeup()) {
    REQUIRE(std::chrono::steady_clock::now() < (now + std::chrono::seconds(1)));
  }
  REQUIRE(base.try_submit([](::io_uring_sqe& sqe) noexcept {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_NOP;
  }));
  std::error_code ec;
  const auto submitted = base.enter(
    1,
    0,
    IORING_ENTER_SQ_WAKEUP,
    nullptr,
    0,
    ec);
  while (!base.need_wakeup()) {
    REQUIRE(std::chrono::steady_clock::now() < (now + std::chrono::seconds(1)));
  }
  REQUIRE(!ec);
  CHECK(submitted == 1);
}

TEST_CASE("When the kernel thread goes to sleep it can be awoken with "
  "io_uring_enter while also submitting and waiting for I/O", "[io_uring][io_uring_context]")
{
  detail::io_uring_context::base base(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 20;
      return retr;
    }());
  CHECK(!base.need_wakeup());
  const auto now = std::chrono::steady_clock::now();
  while (!base.need_wakeup()) {
    REQUIRE(std::chrono::steady_clock::now() < (now + std::chrono::seconds(1)));
  }
  REQUIRE(base.try_submit([](::io_uring_sqe& sqe) noexcept {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_NOP;
  }));
  std::error_code ec;
  const auto submitted = base.enter(
    1,
    1,
    IORING_ENTER_SQ_WAKEUP | IORING_ENTER_GETEVENTS,
    nullptr,
    0,
    ec);
  while (!base.need_wakeup()) {
    REQUIRE(std::chrono::steady_clock::now() < (now + std::chrono::seconds(1)));
  }
  REQUIRE(!ec);
  CHECK(submitted == 1);
  CHECK(base.try_complete([](const ::io_uring_cqe& cqe) {
    CHECK(cqe.res == 0);
  }));
}

TEST_CASE("When SQEs aren't available operations can be enqueued in an atomic, "
  "intrusive linked list", "[io_uring][io_uring_context]")
{
  struct submittable : detail::io_uring_context::submittable {
    virtual bool submit(::io_uring_sqe& sqe) noexcept override {
      submitted = true;
      std::memset(&sqe, 0, sizeof(sqe));
      sqe.opcode = IORING_OP_NOP;
      return true;
    }
    bool submitted{false};
  };
  submittable a;
  submittable b;
  detail::io_uring_context::with_submittable_queue<
    detail::io_uring_context::base> ctx(
      32,
      []() noexcept {
        ::io_uring_params retr{};
        retr.flags = IORING_SETUP_SQPOLL;
        retr.sq_thread_idle = 5000;
        return retr;
      }());
  ctx.enqueue(a);
  ctx.enqueue(b);
  CHECK(!a.submitted);
  CHECK(!b.submitted);
  ctx.dequeue();
  CHECK(a.submitted);
  CHECK(b.submitted);
  std::size_t completed = 0;
  poll_until(
    ctx,
    [&]() {
      (void)ctx.try_complete([&](const ::io_uring_cqe& cqe) {
        CHECK(cqe.res == 0);
        ++completed;
      });
      return completed == 2;
    });
}

TEST_CASE("More tasks can be awaiting a SQE than there are SQEs (i.e. it's not "
  "necessary that all pending tasks be submittable in a single shot",
  "[io_uring][io_uring_context]")
{
  struct submittable : detail::io_uring_context::submittable {
    virtual bool submit(::io_uring_sqe& sqe) noexcept override {
      submitted = true;
      std::memset(&sqe, 0, sizeof(sqe));
      sqe.opcode = IORING_OP_NOP;
      return true;
    }
    bool submitted{false};
  };
  std::vector<submittable> v(2048);
  detail::io_uring_context::with_submittable_queue<
    detail::io_uring_context::base> ctx(
      32,
      []() noexcept {
        ::io_uring_params retr{};
        retr.flags = IORING_SETUP_SQPOLL;
        retr.sq_thread_idle = 5000;
        return retr;
      }());
  for (auto&& task : v) {
    ctx.enqueue(task);
  }
  const auto submitted = [](const submittable& s) noexcept {
    return s.submitted;
  };
  CHECK(std::none_of(v.begin(), v.end(), submitted));
  ctx.dequeue();
  CHECK(std::any_of(v.begin(), v.end(), submitted));
  CHECK(!std::all_of(v.begin(), v.end(), submitted));
  std::size_t completed = 0;
  poll_until(
    ctx,
    [&]() {
      (void)ctx.try_complete([&](const ::io_uring_cqe& cqe) {
        CHECK(cqe.res == 0);
        ++completed;
      });
      ctx.dequeue();
      return completed == v.size();
    });
}

TEST_CASE("Scheduling works via the intrusive linked list of items awaiting "
  "submission", "[io_uring][io_uring_context]")
{
  using context_type = detail::io_uring_context::with_scheduler<
    detail::io_uring_context::with_submittable_queue<
      detail::io_uring_context::base>>;
  context_type ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  const auto scheduler = ctx.get_scheduler();
  CHECK(scheduler == ctx.get_scheduler());
  {
    context_type other(1, {});
    CHECK(!(scheduler == other.get_scheduler()));
    CHECK(scheduler != other.get_scheduler());
  }
  static_assert(::stdexec::scheduler<decltype(scheduler)>);
  const auto sender = ::stdexec::schedule(scheduler);
  using completion_signatures = ::stdexec::completion_signatures_of_t<
    const decltype(sender)&,
    ::stdexec::env<>>;
  static_assert(
    std::is_same_v<
      completion_signatures,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t()>>);
  std::atomic<bool> done{false};
  auto op = ::stdexec::connect(
    sender | ::stdexec::then([&]() noexcept {
      done.store(true, std::memory_order_relaxed);
    }),
    expect_void_receiver<>{});
  ::stdexec::start(op);
  detail::io_uring_context::poll(ctx, done);
}

TEST_CASE("Simple, unstoppable I/O works", "[io_uring][io_uring_context]") {
  auto [read, write] = []() {
    int fds[2];
    REQUIRE(::pipe(fds) != -1);
    return std::pair(
      exec::safe_file_descriptor(fds[0]),
      exec::safe_file_descriptor(fds[1]));
  }();
  const unsigned to_write = 5;
  auto prepare_write = [&](::io_uring_sqe& sqe) noexcept {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_WRITE;
    sqe.fd = write.native_handle();
    sqe.off = -1;
    sqe.addr = reinterpret_cast<decltype(sqe.addr)>(&to_write);
    sqe.len = sizeof(to_write);
  };
  unsigned to_read = 0;
  auto prepare_read = [&](::io_uring_sqe& sqe) noexcept {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_READ;
    sqe.fd = read.native_handle();
    sqe.off = -1;
    sqe.addr = reinterpret_cast<decltype(sqe.addr)>(&to_read);
    sqe.len = sizeof(to_read);
  };
  using context_type = detail::io_uring_context::with_submittable_queue<
    detail::io_uring_context::base>;
  context_type ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  detail::io_uring_context::io_sender<context_type, decltype(prepare_read)>
    read_sender(ctx, prepare_read);
  static_assert(detail::io_uring_context::unstoppable_env<::stdexec::env<>>);
  static_assert(
    std::is_same_v<
      ::stdexec::completion_signatures_of_t<
        decltype(read_sender),
        ::stdexec::env<>>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(const ::io_uring_cqe&)>>);
  static_assert(
    std::is_same_v<
      ::stdexec::completion_signatures_of_t<
        const decltype(read_sender)&,
        ::stdexec::env<>>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(const ::io_uring_cqe&)>>);
  detail::io_uring_context::io_sender<context_type, decltype(prepare_write)>
    write_sender(ctx, prepare_write);
  static_assert(detail::io_uring_context::unstoppable_receiver<
    expect_void_receiver<>>);
  auto read_op = ::stdexec::connect(
    read_sender | ::stdexec::then([](const ::io_uring_cqe& cqe) {
      CHECK(cqe.res == sizeof(to_read));
    }),
    expect_void_receiver<>{});
  auto write_op = ::stdexec::connect(
    write_sender | ::stdexec::then([](const ::io_uring_cqe& cqe) {
      CHECK(cqe.res == sizeof(to_write));
    }),
    expect_void_receiver<>{});
  ::stdexec::start(read_op);
  ::stdexec::start(write_op);
  std::size_t completed = 0;
  poll_until(
    ctx,
    [&]() {
      (void)ctx.try_complete([&](const ::io_uring_cqe& cqe) {
        REQUIRE(cqe.user_data);
        auto&& c = *reinterpret_cast<detail::io_uring_context::completable*>(
          cqe.user_data);
        c.complete(cqe);
        ++completed;
      });
      ctx.dequeue();
      return completed >= 2;
    });
  CHECK(completed == 2);
  CHECK(to_read == to_write);
}

TEST_CASE("Stoppable I/O can be stopped", "[io_uring][io_uring_context]") {
  auto [read, write] = []() {
    int fds[2];
    REQUIRE(::pipe(fds) != -1);
    return std::pair(
      exec::safe_file_descriptor(fds[0]),
      exec::safe_file_descriptor(fds[1]));
  }();
  unsigned to_read = 0;
  auto prepare = [&](::io_uring_sqe& sqe) noexcept {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_READ;
    sqe.fd = read.native_handle();
    sqe.off = -1;
    sqe.addr = reinterpret_cast<decltype(sqe.addr)>(&to_read);
    sqe.len = sizeof(to_read);
  };
  ::stdexec::inplace_stop_source source;
  struct env {
    auto query(const ::stdexec::get_stop_token_t&) const noexcept {
      return source_.get_token();
    }
    ::stdexec::inplace_stop_source& source_;
  };
  using context_type = detail::io_uring_context::with_submittable_queue<
    detail::io_uring_context::base>;
  context_type ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  detail::io_uring_context::io_sender<context_type, decltype(prepare)> sender(
    ctx,
    prepare);
  static_assert(!detail::io_uring_context::unstoppable_env<env>);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures_of_t<
        decltype(sender),
        env>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(const ::io_uring_cqe&),
        ::stdexec::set_stopped_t()>>);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures_of_t<
        const decltype(sender)&,
        env>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(const ::io_uring_cqe&),
        ::stdexec::set_stopped_t()>>);
  static_assert(!detail::io_uring_context::unstoppable_receiver<
    expect_stopped_receiver<env>>);
  bool stopped = false;
  auto op = ::stdexec::connect(
    sender | ::stdexec::let_stopped([&]() {
      CHECK(!stopped);
      stopped = true;
      return ::stdexec::just_stopped();
    }),
    expect_stopped_receiver<env>(env{source}));
  ::stdexec::start(op);
  source.request_stop();
  poll_until(
    ctx,
    [&]() {
      (void)ctx.try_complete([&](const ::io_uring_cqe& cqe) {
        REQUIRE(cqe.user_data);
        auto&& c = *reinterpret_cast<detail::io_uring_context::completable*>(
          cqe.user_data);
        c.complete(cqe);
      });
      ctx.dequeue();
      return stopped;
    });
}

TEST_CASE("Stoppable I/O works", "[io_uring][io_uring_context]") {
  auto [read, write] = []() {
    int fds[2];
    REQUIRE(::pipe(fds) != -1);
    return std::pair(
      exec::safe_file_descriptor(fds[0]),
      exec::safe_file_descriptor(fds[1]));
  }();
  const unsigned to_write = 5;
  unsigned to_read = 0;
  ::stdexec::inplace_stop_source source;
  struct env {
    auto query(const ::stdexec::get_stop_token_t&) const noexcept {
      return source_.get_token();
    }
    ::stdexec::inplace_stop_source& source_;
  };
  io_uring_context ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  std::atomic<bool> done{false};
  std::size_t completed = 0;
  const auto finish = [&]() noexcept {
    ++completed;
    if (completed == 2) {
      done.store(true, std::memory_order_relaxed);
    }
  };
  auto write_op = ::stdexec::connect(
    finally(
      ctx.io([&](::io_uring_sqe& sqe) noexcept {
        std::memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_WRITE;
        sqe.fd = write.native_handle();
        sqe.off = -1;
        sqe.addr = reinterpret_cast<decltype(sqe.addr)>(&to_write);
        sqe.len = sizeof(to_write);
      }) | ::stdexec::then([&](const ::io_uring_cqe& cqe) {
        CHECK(cqe.res == sizeof(to_write));
      }),
      ::stdexec::just() | ::stdexec::then(finish)),
    expect_void_receiver(env{source}));
  auto read_op = ::stdexec::connect(
    finally(
      ctx.io([&](::io_uring_sqe& sqe) noexcept {
        std::memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_READ;
        sqe.fd = read.native_handle();
        sqe.off = -1;
        sqe.addr = reinterpret_cast<decltype(sqe.addr)>(&to_read);
        sqe.len = sizeof(to_read);
      }) | ::stdexec::then([&](const ::io_uring_cqe& cqe) {
        CHECK(cqe.res == sizeof(to_read));
      }),
      ::stdexec::just() | ::stdexec::then(finish)),
    expect_void_receiver(env{source}));
  ::stdexec::start(write_op);
  ::stdexec::start(read_op);
  detail::io_uring_context::poll(ctx, done);
  CHECK(to_read == to_write);
}

TEST_CASE("Stopping I/O works with run_on_polled_io_uring", "[io_uring][io_uring_context]") {
  auto [read, write] = []() {
    int fds[2];
    REQUIRE(::pipe(fds) != -1);
    return std::pair(
      exec::safe_file_descriptor(fds[0]),
      exec::safe_file_descriptor(fds[1]));
  }();
  unsigned to_read = 0;
  ::stdexec::inplace_stop_source source;
  auto sender = run_on_polled_io_uring(
    [&](io_uring_context& ctx) {
      return ctx.io([&](::io_uring_sqe& sqe) noexcept {
        std::memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_READ;
        sqe.fd = read.native_handle();
        sqe.off = -1;
        sqe.addr = reinterpret_cast<decltype(sqe.addr)>(&to_read);
        sqe.len = sizeof(to_read);
      }) | ::stdexec::then([](const ::io_uring_cqe&) {
        FAIL("Operation should end with set_stopped");
      }) | ::exec::write_env(
        ::stdexec::prop(
          ::stdexec::get_stop_token,
          source.get_token()));
    },
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  {
    struct env {
      auto query(const ::stdexec::get_stop_token_t&) const noexcept {
        return source_.get_token();
      }
      ::stdexec::inplace_stop_source& source_;
    };
    static_assert(
      set_equivalent<
        ::stdexec::completion_signatures_of_t<
          decltype(sender),
          env>,
        ::stdexec::completion_signatures<
          ::stdexec::set_value_t(),
          ::stdexec::set_stopped_t(),
          //  This is added by ::stdexec::then because our lambda isn't noexcept
          ::stdexec::set_error_t(std::exception_ptr)>>);
    static_assert(
      set_equivalent<
        ::stdexec::completion_signatures_of_t<
          const decltype(sender)&,
          env>,
        ::stdexec::completion_signatures<
          ::stdexec::set_value_t(),
          ::stdexec::set_stopped_t(),
          ::stdexec::set_error_t(std::exception_ptr)>>);
  }
  source.request_stop();
  CHECK(!::stdexec::sync_wait(std::move(sender)));
}

} // namespace
