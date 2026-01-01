/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *                         Copyright (c) 2025 Robert Leahy. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <exec/coroutine_sender.hpp>
#include <exec/lifetime.hpp>
#include <exec/linux/io_uring_context.hpp>
#include <exec/linux/io_uring_tmpfile.hpp>
#include <exec/linux/unlink.hpp>
#include <stdexec/execution.hpp>

#include <filesystem>

#include "catch2/catch.hpp"

namespace {

using namespace exec;

TEST_CASE("tmpfile paths are unique", "[io_uring][tmpfile]") {
  ::stdexec::sync_wait(
    run_on_blocking_io_uring(
      [&](io_uring_context& ctx) {
        return lifetime(
          [&](io_uring_tmpfile::type& first,
              io_uring_tmpfile::type& second) -> coroutine_sender<void>
          {
            CHECK(first.path() != second.path());
            CHECK(std::filesystem::is_regular_file(first.path()));
            CHECK(std::filesystem::is_regular_file(second.path()));
            co_await ::exec::unlink(ctx, first.path().c_str());
            CHECK(!std::filesystem::exists(first.path()));
            co_await ::exec::unlink(ctx, second.path().c_str());
            CHECK(!std::filesystem::exists(second.path()));
            co_return coroutine_sender_void;
          },
          io_uring_tmpfile(ctx, "/tmp/stdexec-XXXXXX"),
          io_uring_tmpfile(ctx, "/tmp/stdexec-XXXXXX"));
      },
      32,
      ::io_uring_params{}));
}

} // namespace
