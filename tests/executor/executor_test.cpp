/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "cpp_components/executor/executor.h"

#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
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
    ASSERT_FALSE(executor.start());
    executor.post([&done]() { done.set_value(); });

    ASSERT_TRUE(wait_ready(done_future));
    executor.stop();
}

TEST(ExecutorTest, dispatch_runs_callback)
{
    std::promise<void> done;
    const auto done_future = done.get_future().share();

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
    executor.post([&executor, &stopped]() {
        executor.stop(true);
        stopped.set_value();
    });

    ASSERT_TRUE(wait_ready(stopped_future));
}

TEST(ExecutorTest, double_stop_is_idempotent)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    executor.stop();
    executor.stop();
    executor.stop(true);
}

TEST(ExecutorTest, force_stop_from_another_thread)
{
    std::promise<void> handler_done;
    const auto handler_done_future = handler_done.get_future().share();

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
    EXPECT_EQ(executor.set_priority(-1), std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(executor.set_priority(executor.get_max_priority() + 1),
        std::make_error_code(std::errc::invalid_argument));
    executor.stop();
}

TEST(ExecutorTest, set_priority_rejects_stopped_executor)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    executor.stop();

    EXPECT_EQ(executor.set_priority(1), std::make_error_code(std::errc::invalid_argument));
}

TEST(ExecutorTest, set_priority_zero_uses_other_policy)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());

    ASSERT_FALSE(executor.set_priority(0));

    int got = -1;
    ASSERT_FALSE(executor.get_priority(got));
    EXPECT_EQ(got, 0);

    executor.stop();
}

TEST(ExecutorTest, get_priority_returns_current_value)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());

    int priority = -1;
    EXPECT_FALSE(executor.get_priority(priority));
    EXPECT_GE(priority, 0);

    executor.stop();
}

TEST(ExecutorTest, get_priority_rejects_stopped_executor)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    executor.stop();

    int priority = -1;
    EXPECT_EQ(executor.get_priority(priority), std::make_error_code(std::errc::invalid_argument));
}

TEST(ExecutorTest, set_cpu_affinity_rejects_invalid_value)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    EXPECT_EQ(executor.set_cpu_affinity(-1), std::make_error_code(std::errc::invalid_argument));
    executor.stop();
}

TEST(ExecutorTest, set_cpu_affinity_rejects_stopped_executor)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    executor.stop();

    EXPECT_EQ(executor.set_cpu_affinity(0), std::make_error_code(std::errc::invalid_argument));
}

TEST(ExecutorTest, set_cpu_affinity_binds_to_cpu_zero)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());

    ASSERT_FALSE(executor.set_cpu_affinity(0));

    executor.stop();
}

TEST(ExecutorTest, thread_control_rejects_executor_stopped_from_its_thread)
{
    std::promise<void> checked;
    const auto checked_future = checked.get_future().share();

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    executor.post([&executor, &checked]() {
        executor.stop();
        int priority = -1;
        EXPECT_EQ(executor.set_priority(0), std::make_error_code(std::errc::invalid_argument));
        EXPECT_EQ(executor.get_priority(priority),
            std::make_error_code(std::errc::invalid_argument));
        EXPECT_EQ(executor.set_cpu_affinity(0), std::make_error_code(std::errc::invalid_argument));
        checked.set_value();
    });

    ASSERT_TRUE(wait_ready(checked_future));
}

TEST(ExecutorTest, ring_without_doorbell_handler_is_noop)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    executor.ring();
    executor.stop();
}

TEST(ExecutorTest, empty_doorbell_handler_means_no_doorbell)
{
    cpp_components::executor::Executor executor {
        cpp_components::executor::Executor::DoorbellHandler {}
    };
    ASSERT_FALSE(executor.start());
    executor.ring();
    executor.stop();
}

TEST(ExecutorTest, doorbell_handler_runs_on_executor_thread_after_ring)
{
    std::atomic<bool> pushed { false };
    std::promise<std::thread::id> handler_thread_id;
    const auto handler_thread_id_future = handler_thread_id.get_future().share();

    cpp_components::executor::Executor executor { [&pushed, &handler_thread_id]() {
        if (pushed.exchange(false)) {
            handler_thread_id.set_value(std::this_thread::get_id());
        }
        return false;
    } };
    ASSERT_FALSE(executor.start());

    pushed.store(true);
    executor.ring();

    ASSERT_TRUE(wait_ready(handler_thread_id_future));
    EXPECT_NE(handler_thread_id_future.get(), std::this_thread::get_id());
    executor.stop();
}

TEST(ExecutorTest, doorbell_handler_is_not_called_without_ring)
{
    std::atomic<int> calls { 0 };

    cpp_components::executor::Executor executor { [&calls]() {
        calls.fetch_add(1);
        return false;
    } };
    ASSERT_FALSE(executor.start());

    // Flush the executor queue: a spurious doorbell call would have run before this.
    std::promise<void> flushed;
    const auto flushed_future = flushed.get_future().share();
    executor.post([&flushed]() { flushed.set_value(); });
    ASSERT_TRUE(wait_ready(flushed_future));

    EXPECT_EQ(calls.load(), 0);
    executor.stop();
}

TEST(ExecutorTest, doorbell_handler_runs_again_while_work_remains)
{
    constexpr int batch_count = 5;
    std::atomic<int> remaining { 0 };
    std::promise<void> drained;
    const auto drained_future = drained.get_future().share();

    cpp_components::executor::Executor executor { [&remaining, &drained]() {
        if (remaining.load() == 0) {
            return false;
        }
        // Drain one batch per call, as a handler with a budget would.
        if (remaining.fetch_sub(1) == 1) {
            drained.set_value();
            return false;
        }
        return true;
    } };
    ASSERT_FALSE(executor.start());

    remaining.store(batch_count);
    executor.ring();

    ASSERT_TRUE(wait_ready(drained_future));
    EXPECT_EQ(remaining.load(), 0);
    executor.stop();
}

TEST(ExecutorTest, doorbell_does_not_lose_wakeups)
{
    constexpr uint64_t item_count = 200'000;
    std::atomic<uint64_t> produced { 0 };
    std::atomic<uint64_t> consumed { 0 };

    cpp_components::executor::Executor executor { [&produced, &consumed]() {
        consumed.store(produced.load(std::memory_order_acquire), std::memory_order_release);
        return false;
    } };
    ASSERT_FALSE(executor.start());

    std::thread producer([&executor, &produced]() {
        for (uint64_t i = 0; i < item_count; ++i) {
            produced.fetch_add(1, std::memory_order_release);
            executor.ring();
            if (i % 64 == 0) {
                // Let the executor go idle so both the armed and the busy paths are hit.
                std::this_thread::yield();
            }
        }
    });
    producer.join();

    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds { 5 };
    while (consumed.load(std::memory_order_acquire) != item_count &&
        std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }

    EXPECT_EQ(consumed.load(), item_count);
    executor.stop();
}

TEST(ExecutorTest, graceful_stop_with_doorbell_handler_does_not_hang)
{
    std::promise<void> stopped;
    const auto stopped_future = stopped.get_future().share();

    cpp_components::executor::Executor executor { []() { return false; } };
    ASSERT_FALSE(executor.start());

    std::thread stopper([&executor, &stopped]() {
        executor.stop();
        stopped.set_value();
    });

    ASSERT_TRUE(wait_ready(stopped_future));
    stopper.join();
}

TEST(ExecutorTest, graceful_stop_from_doorbell_handler_does_not_deadlock)
{
    std::atomic<bool> pushed { false };
    // The handler needs the executor, which exists only after the handler is passed in.
    cpp_components::executor::Executor *self = nullptr;
    std::promise<void> stopped;
    const auto stopped_future = stopped.get_future().share();

    cpp_components::executor::Executor executor { [&self, &pushed, &stopped]() {
        if (pushed.exchange(false)) {
            self->stop();
            stopped.set_value();
        }
        return false;
    } };
    ASSERT_FALSE(executor.start());
    self = &executor;

    pushed.store(true);
    executor.ring();

    ASSERT_TRUE(wait_ready(stopped_future));
}

TEST(ExecutorTest, destroying_executor_with_doorbell_handler_does_not_hang)
{
    std::promise<void> destroyed;
    const auto destroyed_future = destroyed.get_future().share();

    std::thread owner([&destroyed]() {
        {
            cpp_components::executor::Executor executor { []() { return false; } };
            ASSERT_FALSE(executor.start());
            executor.ring();
        }
        destroyed.set_value();
    });

    ASSERT_TRUE(wait_ready(destroyed_future));
    owner.join();
}

TEST(ExecutorTest, doorbell_handler_is_not_called_after_stop)
{
    std::atomic<int> calls { 0 };
    std::promise<void> rung;
    const auto rung_future = rung.get_future().share();

    cpp_components::executor::Executor executor { [&calls]() {
        calls.fetch_add(1);
        return false;
    } };
    ASSERT_FALSE(executor.start());

    // stop() then ring() on the executor thread: the doorbell completes while stopping.
    executor.post([&executor, &rung]() {
        executor.stop();
        executor.ring();
        rung.set_value();
    });

    ASSERT_TRUE(wait_ready(rung_future));
    executor.stop(); // joins: every pending handler has run or been dropped
    EXPECT_EQ(calls.load(), 0);
}

TEST(ExecutorTest, doorbell_handler_with_remaining_work_is_not_called_after_stop)
{
    std::atomic<int> calls { 0 };
    cpp_components::executor::Executor *self = nullptr;

    cpp_components::executor::Executor executor { [&self, &calls]() {
        // Stops on the first call but reports remaining work, which would call it again.
        if (calls.fetch_add(1) == 0) {
            self->stop();
        }
        return true;
    } };
    ASSERT_FALSE(executor.start());
    self = &executor;

    executor.ring();

    const auto deadline = std::chrono::steady_clock::now() + wait_timeout;
    while (calls.load() == 0 && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::yield();
    }
    ASSERT_EQ(calls.load(), 1);

    executor.stop();
    EXPECT_EQ(calls.load(), 1);
}

TEST(ExecutorTest, start_rejects_second_start)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    EXPECT_EQ(executor.start(), std::make_error_code(std::errc::invalid_argument));
    executor.stop();
}

TEST(ExecutorTest, start_rejects_stopped_executor)
{
    cpp_components::executor::Executor executor {};
    executor.stop();
    EXPECT_EQ(executor.start(), std::make_error_code(std::errc::invalid_argument));
}

TEST(ExecutorTest, executor_can_be_destroyed_without_start)
{
    cpp_components::executor::Executor executor { []() { return false; } };
    executor.ring();
}

TEST(ExecutorTest, callback_posted_before_start_runs_after_start)
{
    std::promise<void> done;
    const auto done_future = done.get_future().share();

    cpp_components::executor::Executor executor {};
    executor.post([&done]() { done.set_value(); });
    EXPECT_EQ(done_future.wait_for(std::chrono::milliseconds { 50 }), std::future_status::timeout);

    ASSERT_FALSE(executor.start());
    ASSERT_TRUE(wait_ready(done_future));
    executor.stop();
}

TEST(ExecutorTest, thread_control_rejects_executor_not_started)
{
    cpp_components::executor::Executor executor {};
    int priority = -1;
    EXPECT_EQ(executor.set_priority(0), std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(executor.get_priority(priority), std::make_error_code(std::errc::invalid_argument));
    EXPECT_EQ(executor.set_cpu_affinity(0), std::make_error_code(std::errc::invalid_argument));
}

TEST(ExecutorTest, ring_before_start_is_noop)
{
    std::atomic<int> calls { 0 };

    cpp_components::executor::Executor executor { [&calls]() {
        calls.fetch_add(1);
        return false;
    } };
    executor.ring();
    ASSERT_FALSE(executor.start());

    // Flush the executor queue: a doorbell call triggered by the early ring() would run first.
    std::promise<void> flushed;
    const auto flushed_future = flushed.get_future().share();
    executor.post([&flushed]() { flushed.set_value(); });
    ASSERT_TRUE(wait_ready(flushed_future));

    EXPECT_EQ(calls.load(), 0);
    executor.stop();
}

TEST(ExecutorDeathTest, exception_escaping_callback_is_logged_and_aborts)
{
    // The executor runs its own thread, so re-execute the test binary in the death test child.
    GTEST_FLAG_SET(death_test_style, "threadsafe");
    EXPECT_DEATH(
        {
            cpp_components::executor::Executor executor {};
            if (executor.start()) {
                return;
            }
            executor.post([]() { throw std::runtime_error("boom"); });
            executor.stop();
        },
        "Executor: unhandled exception in a callback: boom");
}
