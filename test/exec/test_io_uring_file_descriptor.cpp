#include <exec/linux/io_uring_file_descriptor.hpp>

#include <filesystem>
#include <new>
#include <sstream>
#include <type_traits>
#include <utility>
#include <exec/linux/io_uring_context.hpp>
#include <exec/with.hpp>
#include <stdexec/execution.hpp>
#include <unistd.h>

#include "catch2/catch.hpp"

namespace {

using namespace exec;

static_assert(!std::is_move_constructible_v<io_uring_file_descriptor::type>);
static_assert(!std::is_copy_constructible_v<io_uring_file_descriptor::type>);
static_assert(!std::is_move_assignable_v<io_uring_file_descriptor::type>);
static_assert(!std::is_copy_assignable_v<io_uring_file_descriptor::type>);

TEST_CASE("io_uring file descriptors are asynchronously destroyed", "[io_uring][io_uring_file_descriptor]") {
  int fd = -1;
  struct object : io_uring_file_descriptor {
    explicit object(
      io_uring_context& ctx,
      const int fd) noexcept
        : io_uring_file_descriptor(ctx),
          fd_(fd)
    {}
    auto construct(void* const storage) noexcept {
      return
        ::stdexec::just() |
        ::stdexec::then([&, storage]() noexcept {
          new(storage) type(ctx_, fd_);
        });
    }
  private:
    int fd_;
  };
  static_assert(object_in<object, ::stdexec::env<>>);
  ::stdexec::sync_wait(
    run_on_polled_io_uring(
      [&](io_uring_context& ctx) {
        int fds[2];
        REQUIRE(::pipe(fds) != -1);
        ::close(fds[0]);
        fd = fds[1];
        return with(
          ::stdexec::just(),
          object(ctx, fd));
      },
      32,
      []() noexcept {
        ::io_uring_params retr{};
        retr.flags = IORING_SETUP_SQPOLL;
        retr.sq_thread_idle = 5000;
        return retr;
      }()));
  CHECK(fd != -1);
  std::ostringstream ss;
  ss << "/proc/self/fd/" << fd;
  CHECK(!std::filesystem::exists(std::move(ss).str()));
}

} // namespace
