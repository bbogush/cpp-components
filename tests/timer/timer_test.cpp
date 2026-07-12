/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "timer/timer.h"

#include <gtest/gtest.h>

#include <chrono>
#include <future>
#include <system_error>
#include <thread>

namespace {

constexpr auto wait_timeout = std::chrono::seconds { 1 };
constexpr auto short_delay = std::chrono::milliseconds { 20 };

template<typename T>
bool wait_ready(const std::shared_future<T> &future)
{
    return future.wait_for(wait_timeout) == std::future_status::ready;
}

} // namespace

TEST(TimerTest, expires_after_fires_handler)
{
    std::promise<std::error_code> done;
    const auto done_future = done.get_future().share();

    cpp_components::executor::Executor executor {};
    cpp_components::timer::Timer timer { executor };

    timer.expires_after(short_delay);
    timer.async_wait([&done](const std::error_code &ec) { done.set_value(ec); });

    ASSERT_TRUE(wait_ready(done_future));
    EXPECT_FALSE(done_future.get());
    executor.stop();
}

TEST(TimerTest, expires_at_fires_handler)
{
    std::promise<std::error_code> done;
    const auto done_future = done.get_future().share();

    cpp_components::executor::Executor executor {};
    cpp_components::timer::Timer timer { executor };

    const auto deadline = std::chrono::steady_clock::now() + short_delay;
    timer.expires_at(deadline);
    timer.async_wait([&done](const std::error_code &ec) { done.set_value(ec); });

    ASSERT_TRUE(wait_ready(done_future));
    EXPECT_FALSE(done_future.get());
    executor.stop();
}

TEST(TimerTest, expiry_matches_expires_at)
{
    cpp_components::executor::Executor executor {};
    cpp_components::timer::Timer timer { executor };

    const auto deadline = std::chrono::steady_clock::now() + short_delay;
    timer.expires_at(deadline);
    EXPECT_EQ(timer.expiry(), deadline);

    executor.stop();
}

TEST(TimerTest, cancel_invokes_handler_with_operation_canceled)
{
    std::promise<std::error_code> done;
    const auto done_future = done.get_future().share();

    cpp_components::executor::Executor executor {};
    cpp_components::timer::Timer timer { executor };

    timer.expires_after(std::chrono::seconds { 30 });
    timer.async_wait([&done](const std::error_code &ec) { done.set_value(ec); });

    timer.cancel();

    ASSERT_TRUE(wait_ready(done_future));
    EXPECT_EQ(done_future.get(), std::errc::operation_canceled);
    executor.stop();
}

TEST(TimerTest, expires_after_cancels_pending_wait)
{
    std::promise<std::error_code> first_done;
    const auto first_done_future = first_done.get_future().share();
    std::promise<std::error_code> second_done;
    const auto second_done_future = second_done.get_future().share();

    cpp_components::executor::Executor executor {};
    cpp_components::timer::Timer timer { executor };

    timer.expires_after(std::chrono::seconds { 30 });
    timer.async_wait([&first_done](const std::error_code &ec) { first_done.set_value(ec); });

    timer.expires_after(short_delay);
    timer.async_wait([&second_done](const std::error_code &ec) { second_done.set_value(ec); });

    ASSERT_TRUE(wait_ready(first_done_future));
    EXPECT_EQ(first_done_future.get(), std::errc::operation_canceled);

    ASSERT_TRUE(wait_ready(second_done_future));
    EXPECT_FALSE(second_done_future.get());
    executor.stop();
}

TEST(TimerTest, async_wait_with_empty_handler_does_not_crash)
{
    std::promise<void> started;
    const auto started_future = started.get_future().share();

    cpp_components::executor::Executor executor {};
    cpp_components::timer::Timer timer { executor };

    timer.expires_after(short_delay);
    timer.async_wait(nullptr);

    // Give the timer time to fire with an empty handler, then stop cleanly.
    executor.post([&started]() { started.set_value(); });
    ASSERT_TRUE(wait_ready(started_future));
    std::this_thread::sleep_for(short_delay * 2);
    executor.stop();
}

TEST(TimerTest, destructor_cancels_pending_wait)
{
    std::promise<std::error_code> done;
    const auto done_future = done.get_future().share();

    cpp_components::executor::Executor executor {};
    {
        cpp_components::timer::Timer timer { executor };
        timer.expires_after(std::chrono::seconds { 30 });
        timer.async_wait([&done](const std::error_code &ec) { done.set_value(ec); });
    }

    ASSERT_TRUE(wait_ready(done_future));
    EXPECT_EQ(done_future.get(), std::errc::operation_canceled);
    executor.stop();
}

TEST(TimerTest, handler_runs_on_executor_thread)
{
    std::promise<std::thread::id> handler_thread_id;
    const auto handler_thread_id_future = handler_thread_id.get_future().share();
    const auto main_thread_id = std::this_thread::get_id();

    cpp_components::executor::Executor executor {};
    cpp_components::timer::Timer timer { executor };

    timer.expires_after(short_delay);
    timer.async_wait([&handler_thread_id](const std::error_code &) {
        handler_thread_id.set_value(std::this_thread::get_id());
    });

    ASSERT_TRUE(wait_ready(handler_thread_id_future));
    EXPECT_NE(handler_thread_id_future.get(), main_thread_id);
    executor.stop();
}
