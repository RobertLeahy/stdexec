#include <exec/linux/io_uring_socket.hpp>

#include <exec/linux/io_uring_context.hpp>
#include <exec/linux/io_uring_open.hpp>
#include <exec/linux/safe_file_descriptor.hpp>

#include <stdlib.h>

#include "catch2/catch.hpp"

namespace {

using namespace exec;

static_assert(object_in<io_uring_socket, ::stdexec::env<>>);

TEST_CASE("io_uring filesystem operations work", "[io_uring][io_uring_file_descriptor][io_uring_open]") {
  std::string path("/tmp/XXXXXX");
  safe_file_descriptor fd(::mkstemp(path.data()));
  REQUIRE(fd);
  //  TODO
}

} // namespace
