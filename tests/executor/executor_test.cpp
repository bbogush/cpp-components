/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "executor/executor.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <future>
#include <system_error>
#include <thread>

namespace {

constexpr auto wait_timeout = std::chrono::seconds { 1 };

template<typename T>
bool wait_ready(const std::shared_future<T> &future)
{
    return future.wait_for(wait_timeout) == std::future_status::ready;
}

} // namespace

TEST(ExecutorTest, post_runs_callback)
{
    std::promise<void> done;
    const auto done_future = done.get_future().share();

    cpp_components::executor::Executor executor {};
    executor.post([&done]() { done.set_value(); });

    ASSERT_TRUE(wait_ready(done_future));
    executor.stop();
}

TEST(ExecutorTest, dispatch_runs_callback)
{
    std::promise<void> done;
    const auto done_future = done.get_future().share();

    cpp_components::executor::Executor executor {};
    executor.dispatch([&done]() { done.set_value(); });

    ASSERT_TRUE(wait_ready(done_future));
    executor.stop();
}

TEST(ExecutorTest, post_runs_on_executor_thread)
{
    std::promise<std::thread::id> executor_thread_id;
    const auto executor_thread_id_future = executor_thread_id.get_future().share();
    const auto main_thread_id = std::this_thread::get_id();

    cpp_components::executor::Executor executor {};
    executor.post(
        [&executor_thread_id]() { executor_thread_id.set_value(std::this_thread::get_id()); });

    ASSERT_TRUE(wait_ready(executor_thread_id_future));
    EXPECT_NE(executor_thread_id_future.get(), main_thread_id);
    executor.stop();
}

TEST(ExecutorTest, get_context_accepts_posted_handlers)
{
    std::promise<void> done;
    const auto done_future = done.get_future().share();

    cpp_components::executor::Executor executor {};
    boost::asio::post(executor.get_context(), [&done]() { done.set_value(); });

    ASSERT_TRUE(wait_ready(done_future));
    executor.stop();
}

TEST(ExecutorTest, graceful_stop_drains_posted_handlers)
{
    constexpr int handler_count = 10;
    std::atomic<int> completed_handlers { 0 };

    std::promise<void> last_handler_done;
    const auto last_handler_done_future = last_handler_done.get_future().share();

    cpp_components::executor::Executor executor {};
    for (int i = 0; i < handler_count; ++i) {
        executor.post([&completed_handlers]() { completed_handlers.fetch_add(1); });
    }
    executor.post([&last_handler_done]() { last_handler_done.set_value(); });

    ASSERT_TRUE(wait_ready(last_handler_done_future));
    executor.stop();

    EXPECT_EQ(completed_handlers.load(), handler_count);
}

TEST(ExecutorTest, graceful_stop_from_executor_thread_does_not_deadlock)
{
    std::promise<void> stopped;
    const auto stopped_future = stopped.get_future().share();

    cpp_components::executor::Executor executor {};
    executor.post([&executor, &stopped]() {
        executor.stop();
        stopped.set_value();
    });

    ASSERT_TRUE(wait_ready(stopped_future));
}

TEST(ExecutorTest, force_stop_from_executor_thread_does_not_deadlock)
{
    std::promise<void> stopped;
    const auto stopped_future = stopped.get_future().share();

    cpp_components::executor::Executor executor {};
    executor.post([&executor, &stopped]() {
        executor.stop(true);
        stopped.set_value();
    });

    ASSERT_TRUE(wait_ready(stopped_future));
}

TEST(ExecutorTest, double_stop_is_idempotent)
{
    cpp_components::executor::Executor executor {};
    executor.stop();
    executor.stop();
    executor.stop(true);
}

TEST(ExecutorTest, force_stop_from_another_thread)
{
    std::promise<void> handler_done;
    const auto handler_done_future = handler_done.get_future().share();

    cpp_components::executor::Executor executor {};
    executor.post([&handler_done]() { handler_done.set_value(); });

    ASSERT_TRUE(wait_ready(handler_done_future));

    std::promise<void> stop_done;
    const auto stop_done_future = stop_done.get_future().share();
    std::thread stopper([&executor, &stop_done]() {
        executor.stop(true);
        stop_done.set_value();
    });

    ASSERT_TRUE(wait_ready(stop_done_future));
    stopper.join();
}

TEST(ExecutorTest, set_priority_rejects_invalid_value)
{
    cpp_components::executor::Executor executor {};
    EXPECT_EQ(executor.set_priority(-1), std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(executor.set_priority(executor.get_max_priority() + 1),
        std::make_error_code(std::errc::invalid_argument));
    executor.stop();
}

TEST(ExecutorTest, set_priority_rejects_stopped_executor)
{
    cpp_components::executor::Executor executor {};
    executor.stop();

    EXPECT_EQ(executor.set_priority(1), std::make_error_code(std::errc::invalid_argument));
}

TEST(ExecutorTest, set_priority_zero_uses_other_policy)
{
    cpp_components::executor::Executor executor {};

    ASSERT_FALSE(executor.set_priority(0));

    int got = -1;
    ASSERT_FALSE(executor.get_priority(got));
    EXPECT_EQ(got, 0);

    executor.stop();
}

TEST(ExecutorTest, get_priority_returns_current_value)
{
    cpp_components::executor::Executor executor {};

    int priority = -1;
    EXPECT_FALSE(executor.get_priority(priority));
    EXPECT_GE(priority, 0);

    executor.stop();
}

TEST(ExecutorTest, get_priority_rejects_stopped_executor)
{
    cpp_components::executor::Executor executor {};
    executor.stop();

    int priority = -1;
    EXPECT_EQ(executor.get_priority(priority), std::make_error_code(std::errc::invalid_argument));
}
