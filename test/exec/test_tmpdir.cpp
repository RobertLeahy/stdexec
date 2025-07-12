/*
 * SPDX-FileCopyrightText: Copyright (c) 2025 NVIDIA CORPORATION & AFFILIATES. All rights reserved.
 *                         Copyright (c) 2025 Robert Leahy. All rights reserved.
 * SPDX-License-Identifier: Apache-2.0 WITH LLVM-exception
 */

#include <exec/coroutine_sender.hpp>
#include <exec/lifetime.hpp>
#include <exec/linux/io_uring_context.hpp>
#include <exec/linux/tmpdir.hpp>
#include <exec/linux/unlink.hpp>
#include <stdexec/execution.hpp>

#include <filesystem>
#include <string>

#include "catch2/catch.hpp"

namespace {

using namespace exec;

TEST_CASE("tmpdir creates a directory and returns the path", "[io_uring][tmpdir]") {
  ::stdexec::sync_wait(
    run_on_blocking_io_uring(
      [&](io_uring_context& ctx) -> coroutine_sender<void> {
        const auto a = co_await ::exec::tmpdir(ctx, "/tmp/stdexec-tmpdir-XXXXXX");
        const auto b = co_await ::exec::tmpdir(ctx, "/tmp/stdexec-tmpdir-XXXXXX");
        CHECK(std::filesystem::is_directory(a));
        CHECK(std::filesystem::is_directory(b));
        CHECK(a != b);
        co_await unlink(ctx, a.c_str(), AT_REMOVEDIR);
        CHECK(!std::filesystem::exists(a));
        co_await unlink(ctx, b.c_str(), AT_REMOVEDIR);
        CHECK(!std::filesystem::exists(b));
        co_return coroutine_sender_void;
      },
      32,
      ::io_uring_params{}));
}

} // namespace
