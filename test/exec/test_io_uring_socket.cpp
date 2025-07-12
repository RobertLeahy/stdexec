#include <exec/linux/io_uring_socket.hpp>

#include <algorithm>
#include <cstddef>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <exec/linux/bind.hpp>
#include <exec/linux/connect.hpp>
#include <exec/linux/listen.hpp>
#include <exec/linux/read.hpp>
#include <exec/linux/io_uring_accept.hpp>
#include <exec/linux/io_uring_context.hpp>
#include <exec/linux/write.hpp>
#include <exec/lifetime.hpp>
#include <exec/sequence.hpp>
#include <exec/sync_object.hpp>
#include <stdexec/execution.hpp>
#include <netinet/in.h>
#include <sys/socket.h>

#include <sys/socket.h>

#include "catch2/catch.hpp"

namespace {

using namespace exec;

static_assert(object_in<io_uring_socket, ::stdexec::env<>>);

TEST_CASE("io_uring sockets are asynchronously created and destroyed", "[io_uring][io_uring_file_descriptor][io_uring_socket]") {
  int fd = -1;
  const auto exists = [&]() {
    std::ostringstream ss;
    ss << "/proc/self/fd/" << fd;
    return std::filesystem::exists(std::move(ss).str());
  };
  ::stdexec::sync_wait(
    run_on_polled_io_uring(
      [&](io_uring_context& ctx) {
        return lifetime(
          [&](io_uring_socket::type& socket) {
            fd = socket.native_handle();
            CHECK(fd != -1);
            CHECK(exists());
            return ::stdexec::just();
          },
          io_uring_socket(
            ctx,
            AF_INET,
            SOCK_STREAM,
            IPPROTO_TCP));
      },
      32,
      []() noexcept {
        ::io_uring_params retr{};
        retr.flags = IORING_SETUP_SQPOLL;
        retr.sq_thread_idle = 5000;
        return retr;
      }()));
  CHECK(fd != -1);
  CHECK(!exists());
}

TEST_CASE("io_uring sockets fail to construct when the arguments thereto make no sense", "[io_uring][io_uring_file_descriptor][io_uring_socket]") {
  std::size_t invoked = 0;
  CHECK_THROWS(
    ::stdexec::sync_wait(
      run_on_polled_io_uring(
        [&](io_uring_context& ctx) {
          return lifetime(
            [](auto&&...) {
              FAIL_CHECK("Unexpected invocation");
              return ::stdexec::just();
            },
            io_uring_socket(
              ctx,
              AF_INET,
              //  Stream & UDP make no sense together
              SOCK_STREAM,
              IPPROTO_UDP));
        },
        32,
        []() noexcept {
          ::io_uring_params retr{};
          retr.flags = IORING_SETUP_SQPOLL;
          retr.sq_thread_idle = 5000;
          return retr;
        }())));
  CHECK(invoked == 0);
}

TEST_CASE("io_uring sockets can be connected to one another", "[io_uring][io_uring_file_descriptor][io_uring_socket]") {
  const std::string_view sv("Hello world!");
  std::vector<std::byte> buffer(sv.size());
  ::stdexec::sync_wait(
    run_on_polled_io_uring(
      [&](io_uring_context& ctx) {
        const io_uring_socket object(
          ctx,
          AF_INET,
          SOCK_STREAM,
          IPPROTO_TCP);
        return lifetime(
          [&](
            io_uring_socket::type& server,
            io_uring_socket::type& client,
            ::sockaddr_in& addr)
          {
            std::memset(&addr, 0, sizeof(addr));
            addr.sin_family = AF_INET;
            (void)client;
            return
              ::exec::sequence(
                ::exec::bind(server, addr),
                ::exec::listen(server, SOMAXCONN)) |
              ::stdexec::let_value([&]() {
                //  Not available in an async version
                ::socklen_t out = sizeof(addr);
                if (::getsockname(
                  server.native_handle(),
                  reinterpret_cast<::sockaddr*>(&addr),
                  &out) == -1)
                {
                  throw std::runtime_error("getsockname failed");
                }
                return ::stdexec::when_all(
                  ::exec::sequence(
                    ::exec::connect(client, addr),
                    ::exec::write(
                      client,
                      std::span<const std::byte>(
                        reinterpret_cast<const std::byte*>(sv.data()),
                        sv.size()))),
                  ::exec::lifetime(
                    [&](io_uring_accept::type& accepted) {
                      return ::exec::read(accepted, buffer);
                    },
                    io_uring_accept(server)));
              });
          },
          //  Constructs two sockets
          object,
          object,
          ::exec::sync_object<::sockaddr_in>());
      },
      32,
      []() noexcept {
        ::io_uring_params retr{};
        retr.flags = IORING_SETUP_SQPOLL;
        retr.sq_thread_idle = 5000;
        return retr;
      }()));
  const auto ptr = reinterpret_cast<const char*>(buffer.data());
  CHECK(std::equal(sv.begin(), sv.end(), ptr, ptr + buffer.size()));
}

} // namespace
