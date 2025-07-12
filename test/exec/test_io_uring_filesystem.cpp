#include <exec/coroutine_sender.hpp>
#include <exec/lifetime.hpp>
#include <exec/object.hpp>
#include <exec/linux/io_uring_context.hpp>
#include <exec/linux/open_object.hpp>
#include <exec/linux/read.hpp>
#include <exec/linux/rename.hpp>
#include <exec/linux/safe_file_descriptor.hpp>
#include <exec/linux/stat.hpp>
#include <exec/linux/sync.hpp>
#include <exec/linux/unlink.hpp>
#include <exec/linux/write.hpp>
#include <stdexec/execution.hpp>

#include <stdlib.h>
#include <cstddef>
#include <span>
#include <string>
#include <string_view>
#include <system_error>

#include "catch2/catch.hpp"

namespace {

using namespace exec;

static_assert(object_in<open_object, ::stdexec::env<>>);

TEST_CASE("io_uring filesystem operations work", "[io_uring][io_uring_file_descriptor][io_uring_open]") {
  const std::string_view sv("foobarbaz");
  const std::span span(reinterpret_cast<const std::byte*>(sv.data()), sv.size());
  const auto mktemp = [&]() {
    std::string path("/tmp/XXXXXX");
    const safe_file_descriptor fd(::mkstemp(path.data()));
    REQUIRE(fd);
    return path;
  };
  const auto path = mktemp();
  const auto other_path = mktemp();
  const auto snd = run_on_blocking_io_uring(
    [&](io_uring_context& ctx) {
      return lifetime(
        [&](open_object::type& fd) -> coroutine_sender<void> {
          const auto a = co_await stat(fd);
          CHECK(a.stx_size == 0);
          const auto b = co_await stat(ctx, other_path.c_str());
          CHECK(a.stx_ino != b.stx_ino);
          co_await write(fd, span);
          co_await write(fd, span.subspan(0, 3), 3);
          std::string str;
          str.resize(span.size());
          co_await read(fd, std::span(reinterpret_cast<std::byte*>(str.data()), str.size()), 0);
          CHECK(str == "foofoobaz");
          str.resize(3);
          co_await read(fd, std::span(reinterpret_cast<std::byte*>(str.data()), str.size()), 6);
          CHECK(str == "baz");
          co_await rename(ctx, path.c_str(), other_path.c_str());
          CHECK_THROWS_AS(co_await stat(ctx, path.c_str()), std::system_error);
          co_await datasync(fd);
          const auto c = co_await stat(ctx, other_path.c_str());
          CHECK(a.stx_ino == c.stx_ino);
          CHECK(c.stx_size == span.size());
          co_return coroutine_sender_void;
        },
        open_object(ctx, path.c_str(), O_RDWR));
    },
    32,
    ::io_uring_params{});
  ::stdexec::sync_wait(snd);
}

} // namespace
