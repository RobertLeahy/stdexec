#include <exec/linux/io_uring_context.hpp>

#include <linux/io_uring.h>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <cstring>
#include <exception>
#include <functional>
#include <iterator>
#include <system_error>
#include <thread>
#include <type_traits>
#include <vector>
#include <exec/finally.hpp>
#include <exec/linux/safe_file_descriptor.hpp>
#include <sys/timerfd.h>
#include "../test_common/receivers.hpp"
#include "../test_common/type_helpers.hpp"

#include "catch2/catch.hpp"

namespace {

using namespace exec;

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
    virtual void submit(::io_uring_sqe& sqe) noexcept override {
      submitted = true;
      std::memset(&sqe, 0, sizeof(sqe));
      sqe.opcode = IORING_OP_NOP;
      ctx->consume_sqe();
    }
    io_uring_context* ctx{};
    bool submitted{false};
  };
  submittable a;
  submittable b;
  io_uring_context ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  a.ctx = &ctx;
  b.ctx = &ctx;
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
    virtual void submit(::io_uring_sqe& sqe) noexcept override {
      submitted = true;
      std::memset(&sqe, 0, sizeof(sqe));
      sqe.opcode = IORING_OP_NOP;
      ctx->consume_sqe();
    }
    io_uring_context* ctx{};
    bool submitted{false};
  };
  submittable arr[2048];
  io_uring_context ctx(
      32,
      []() noexcept {
        ::io_uring_params retr{};
        retr.flags = IORING_SETUP_SQPOLL;
        retr.sq_thread_idle = 5000;
        return retr;
      }());
  for (auto&& submittable : arr) {
    submittable.ctx = &ctx;
  }
  for (auto&& task : arr) {
    ctx.enqueue(task);
  }
  auto op = ::stdexec::connect(
    ctx.wait_for_sqe() | ::stdexec::then([&](auto&& sqe) noexcept {
      std::memset(&sqe, 0, sizeof(sqe));
      sqe.opcode = IORING_OP_NOP;
      ctx.consume_sqe();
    }),
    expect_void_receiver{});
  auto op2 = ::stdexec::connect(
    ctx.get_or_wait_for_sqe() | ::stdexec::then([&](auto&& sqe) noexcept {
      std::memset(&sqe, 0, sizeof(sqe));
      sqe.opcode = IORING_OP_NOP;
      ctx.consume_sqe();
    }),
    expect_void_receiver{});
  const auto submitted = [](const submittable& s) noexcept {
    return s.submitted;
  };
  using std::begin;
  using std::end;
  CHECK(std::none_of(begin(arr), end(arr), submitted));
  ::stdexec::start(op);
  ::stdexec::start(op2);
  ctx.dequeue();
  CHECK(std::any_of(begin(arr), end(arr), submitted));
  CHECK(!std::all_of(begin(arr), end(arr), submitted));
  std::size_t completed = 0;
  poll_until(
    ctx,
    [&]() {
      (void)ctx.try_complete([&](const ::io_uring_cqe& cqe) {
        //  This branch skips any implementation-detail operations the ring uses
        if (cqe.user_data) {
          return;
        }
        CHECK(cqe.res == 0);
        ++completed;
      });
      ctx.dequeue();
      return completed == (std::distance(begin(arr), end(arr)) + 2);
    });
}

TEST_CASE("Scheduling works via the intrusive linked list of items awaiting "
  "submission", "[io_uring][io_uring_context]")
{
  io_uring_context ctx(
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
    io_uring_context other(1, {});
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

TEST_CASE("An asynchronous operation can be used to acquire an SQE, and then to wait for the completion of that work", "[io_uring][io_uring_context]")
{
  io_uring_context ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  std::atomic<bool> done{false};
  auto sender =
    ctx.get_or_wait_for_sqe() |
    ::stdexec::then([](::io_uring_sqe& sqe) noexcept {
      //  This is kludge to work around the fact that let_value decay copies
      return std::ref(sqe);
    }) | 
    ::stdexec::let_value([&](::io_uring_sqe& sqe) noexcept {
      std::memset(&sqe, 0, sizeof(sqe));
      sqe.opcode = IORING_OP_NOP;
      return ctx.wait_for_completion(sqe);
    }) |
    ::stdexec::then([&](const ::io_uring_cqe& cqe) {
      CHECK(cqe.res == 0);
      done.store(true, std::memory_order_relaxed);
    });
  auto op = ::stdexec::connect(
    std::move(sender),
    expect_void_receiver{});
  ::stdexec::start(op);
  detail::io_uring_context::poll(ctx, done);
}

TEST_CASE("Operations can be cancelled", "[io_uring][io_uring_context]") {
  io_uring_context ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  auto [read, write] = []() {
    int fds[2];
    REQUIRE(::pipe(fds) != -1);
    return std::pair(
      exec::safe_file_descriptor(fds[0]),
      exec::safe_file_descriptor(fds[1]));
  }();
  unsigned to_read = 0;
  const auto sqe = ctx.get_sqe();
  REQUIRE(sqe);
  std::memset(sqe, 0, sizeof(*sqe));
  sqe->opcode = IORING_OP_READ;
  sqe->fd = read.native_handle();
  sqe->off = -1;
  sqe->addr = reinterpret_cast<decltype(sqe->addr)>(&to_read);
  sqe->len = sizeof(to_read);
  bool invoked = false;
  auto op = ::stdexec::connect(
    ctx.wait_for_completion(*sqe),
    make_fun_receiver([&](const ::io_uring_cqe& cqe) {
      invoked = true;
      CHECK(cqe.res < 0);
    }));
  ::stdexec::start(op);
  bool cancel_invoked = false;
  auto cancel_op = ::stdexec::connect(
    ctx.cancel(op),
    make_fun_receiver([&]() noexcept {
      cancel_invoked = true;
    }));
  ::stdexec::start(cancel_op);
  poll_until(
    ctx,
    [&]() {
      ctx.dequeue();
      detail::io_uring_context::complete(ctx);
      return invoked && cancel_invoked;
    });
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
  io_uring_context ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  bool write_invoked = false;
  auto write_sender = ctx.io(prepare_write);
  static_assert(
    std::is_same_v<
      ::stdexec::completion_signatures_of_t<
        decltype(write_sender),
        ::stdexec::env<>>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(const ::io_uring_cqe&)>>);
  auto write_op = ::stdexec::connect(
    write_sender,
    make_fun_receiver([&](const ::io_uring_cqe& cqe) {
      CHECK(!write_invoked);
      write_invoked = true;
      CHECK(cqe.res > 0);
    }));
  bool read_invoked = false;
  auto read_op = ::stdexec::connect(
    ctx.io(prepare_read),
    make_fun_receiver([&](const ::io_uring_cqe& cqe) {
      CHECK(!read_invoked);
      read_invoked = true;
      CHECK(cqe.res > 0);
    }));
  ::stdexec::start(write_op);
  ::stdexec::start(read_op);
  poll_until(
    ctx,
    [&]() {
      ctx.dequeue();
      detail::io_uring_context::complete(ctx);
      return write_invoked && read_invoked;
    });
}

TEST_CASE("Stoppable I/O stops if stop is outstanding before it is started", "[io_uring][io_uring_context]") {
  auto [read, write] = []() {
    int fds[2];
    REQUIRE(::pipe(fds) != -1);
    return std::pair(
      exec::safe_file_descriptor(fds[0]),
      exec::safe_file_descriptor(fds[1]));
  }();
  unsigned to_read = 0;
  auto prepare_read = [&](::io_uring_sqe& sqe) noexcept {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_READ;
    sqe.fd = read.native_handle();
    sqe.off = -1;
    sqe.addr = reinterpret_cast<decltype(sqe.addr)>(&to_read);
    sqe.len = sizeof(to_read);
  };
  ::stdexec::inplace_stop_source source;
  source.request_stop();
  ::stdexec::prop env(
    ::stdexec::get_stop_token,
    source.get_token());
  io_uring_context ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  auto sender = ctx.io(prepare_read);
  static_assert(
    set_equivalent<
      ::stdexec::completion_signatures_of_t<
        decltype(sender),
        decltype(env)>,
      ::stdexec::completion_signatures<
        ::stdexec::set_value_t(const ::io_uring_cqe&),
        ::stdexec::set_stopped_t()>>);
  bool done = false;
  auto op = ::stdexec::connect(
    sender | ::stdexec::let_stopped([&]() noexcept {
      done = true;
      return ::stdexec::just_stopped();
    }),
    expect_stopped_receiver(env));
  ::stdexec::start(op);
  poll_until(
    ctx,
    [&]() {
      ctx.dequeue();
      detail::io_uring_context::complete(ctx);
      return done;
    });
}

TEST_CASE("Stoppable I/O stops if stop is requested after it is started", "[io_uring][io_uring_context]") {
  auto [read, write] = []() {
    int fds[2];
    REQUIRE(::pipe(fds) != -1);
    return std::pair(
      exec::safe_file_descriptor(fds[0]),
      exec::safe_file_descriptor(fds[1]));
  }();
  unsigned to_read = 0;
  auto prepare_read = [&](::io_uring_sqe& sqe) noexcept {
    std::memset(&sqe, 0, sizeof(sqe));
    sqe.opcode = IORING_OP_READ;
    sqe.fd = read.native_handle();
    sqe.off = -1;
    sqe.addr = reinterpret_cast<decltype(sqe.addr)>(&to_read);
    sqe.len = sizeof(to_read);
  };
  ::stdexec::inplace_stop_source source;
  ::stdexec::prop env(
    ::stdexec::get_stop_token,
    source.get_token());
  io_uring_context ctx(
    32,
    []() noexcept {
      ::io_uring_params retr{};
      retr.flags = IORING_SETUP_SQPOLL;
      retr.sq_thread_idle = 5000;
      return retr;
    }());
  bool done = false;
  auto op = ::stdexec::connect(
    ctx.io(prepare_read) | ::stdexec::let_stopped([&]() noexcept {
      done = true;
      return ::stdexec::just_stopped();
    }),
    expect_stopped_receiver(env));
  ::stdexec::start(op);
  source.request_stop();
  poll_until(
    ctx,
    [&]() {
      ctx.dequeue();
      detail::io_uring_context::complete(ctx);
      return done;
    });
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
      }) | ::stdexec::write_env(
        ::stdexec::prop(
          ::stdexec::get_stop_token,
          source.get_token()));
    },
    32,
    []() noexcept {
      ::io_uring_params retr{};
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

TEST_CASE("A timerfd can be waited on a blocking io uring", "[io_uring][io_uring_context]") {
  std::uint64_t expirations = 0;
  exec::safe_file_descriptor fd(::timerfd_create(CLOCK_MONOTONIC, TFD_CLOEXEC));
  REQUIRE(fd);
  const auto start = std::chrono::steady_clock::now();
  {
    ::itimerspec ts{};
    ts.it_value.tv_nsec = 250000000;
    REQUIRE(::timerfd_settime(fd.native_handle(), 0, &ts, nullptr) == 0);
  }
  auto sender = run_on_blocking_io_uring(
    [&](io_uring_context& ctx) {
      return ctx.io([&](::io_uring_sqe& sqe) noexcept {
        CHECK(!expirations);
        std::memset(&sqe, 0, sizeof(sqe));
        sqe.opcode = IORING_OP_READ;
        sqe.fd = fd.native_handle();
        sqe.off = -1;
        sqe.addr = reinterpret_cast<decltype(sqe.addr)>(&expirations);
        sqe.len = sizeof(expirations);
      }) | ::stdexec::then([&](const ::io_uring_cqe& cqe) {
        CHECK(cqe.res == sizeof(expirations));
        CHECK(expirations == 1);
      });
    },
    32,
    ::io_uring_params{});
  CHECK(::stdexec::sync_wait(std::move(sender)));
  const auto end = std::chrono::steady_clock::now();
  const auto expired = end - start;
  CHECK(expired >= std::chrono::milliseconds(250));
}

} // namespace
