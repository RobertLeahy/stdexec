#include <exec/linux/accept_object.hpp>
#include <exec/linux/socket_object.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>
#include <exec/coroutine_sender.hpp>
#include <exec/enter_sender.hpp>
#include <exec/exit_sender.hpp>
#include <exec/linux/bind.hpp>
#include <exec/linux/close.hpp>
#include <exec/linux/connect.hpp>
#include <exec/linux/file_descriptor.hpp>
#include <exec/linux/io_uring_context.hpp>
#include <exec/linux/listen.hpp>
#include <exec/linux/read.hpp>
#include <exec/linux/write.hpp>
#include <exec/lifetime.hpp>
#include <exec/sync_object.hpp>
#include <stdexec/execution.hpp>
#include <netinet/in.h>
#include <sys/socket.h>

#include "catch2/catch.hpp"

#include <exec/enter.hpp>

namespace {

using namespace exec;

static_assert(
  ::exec::exit_sender_in<
    decltype(::exec::close(std::declval<::exec::file_descriptor&>())),
    ::stdexec::env<>>);
static_assert(object_in<accept_object, ::stdexec::env<>>);
static_assert(object_in<socket_object, ::stdexec::env<>>);
static_assert(object_in<::exec::sync_object<::sockaddr_in>, ::stdexec::env<>>);

//TEST_CASE("io_uring sockets are asynchronously created and destroyed", "[io_uring][io_uring_file_descriptor][io_uring_socket]") {
//  int fd = -1;
//  const auto exists = [&]() {
//    std::ostringstream ss;
//    ss << "/proc/self/fd/" << fd;
//    return std::filesystem::exists(std::move(ss).str());
//  };
//  ::stdexec::sync_wait(
//    run_on_blocking_io_uring(
//      [&](io_uring_context& ctx) {
//        return lifetime(
//          [&](io_uring_socket::type& socket) {
//            fd = socket.native_handle();
//            CHECK(fd != -1);
//            CHECK(exists());
//            return ::stdexec::just();
//          },
//          io_uring_socket(
//            ctx,
//            AF_INET,
//            SOCK_STREAM,
//            IPPROTO_TCP));
//      },
//      32,
//      ::io_uring_params{}));
//  CHECK(fd != -1);
//  CHECK(!exists());
//}
//
//TEST_CASE("io_uring sockets fail to construct when the arguments thereto make no sense", "[io_uring][io_uring_file_descriptor][io_uring_socket]") {
//  std::size_t invoked = 0;
//  CHECK_THROWS(
//    ::stdexec::sync_wait(
//      run_on_blocking_io_uring(
//        [&](io_uring_context& ctx) {
//          return lifetime(
//            [](auto&&...) {
//              FAIL_CHECK("Unexpected invocation");
//              return ::stdexec::just();
//            },
//            io_uring_socket(
//              ctx,
//              AF_INET,
//              //  Stream & UDP make no sense together
//              SOCK_STREAM,
//              IPPROTO_UDP));
//        },
//        32,
//        ::io_uring_params{})));
//  CHECK(invoked == 0);
//}

TEST_CASE("io_uring sockets can be connected to one another", "[io_uring][io_uring_file_descriptor][io_uring_socket]") {
  const std::string_view sv("Hello world!");
  std::vector<std::byte> buffer(sv.size());
  ::stdexec::sync_wait(
    run_on_blocking_io_uring(
      [&](io_uring_context& ctx) {
        (void)ctx;
        const socket_object object(
          ctx,
          AF_INET,
          SOCK_STREAM,
          IPPROTO_TCP);
        return lifetime(
          [&](
            socket_object::type& server,
            socket_object::type& client,
            ::sockaddr_in& addr) -> coroutine_sender<void>
          {
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            (void)client;
            co_await ::exec::bind(server, addr);
            co_await ::exec::listen(server, SOMAXCONN);
            ::socklen_t out = sizeof(addr);
            if (::getsockname(
              server.native_handle(),
              reinterpret_cast<::sockaddr*>(&addr),
              &out) == -1)
            {
              throw std::runtime_error("getsockname failed");
            }
            co_await ::stdexec::when_all(
              [&]() -> coroutine_sender<void> {
                co_await ::exec::connect(client, addr);
                co_await ::exec::write(
                  client,
                  std::span<const std::byte>(
                    reinterpret_cast<const std::byte*>(sv.data()),
                    sv.size()));
                co_return coroutine_sender_void;
              }(),
              ::exec::lifetime(
                [&](accept_object::type& accepted) {
                  return ::exec::read(accepted, buffer);
                },
                accept_object(server)));
            co_return coroutine_sender_void;
          },
          //  Constructs two sockets
          object,
          object,
          ::exec::sync_object<::sockaddr_in>());
      },
      32,
      ::io_uring_params{}));
  const auto ptr = reinterpret_cast<const char*>(buffer.data());
  CHECK(std::equal(sv.begin(), sv.end(), ptr, ptr + buffer.size()));
}

} // namespace
