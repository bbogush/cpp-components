/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "cpp_components/executor/executor.h"

#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <memory>
#include <thread>

namespace {

constexpr int callback_count = 4'000'000;
constexpr auto wait_timeout = std::chrono::seconds { 5 };

class PostFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State & /*state*/) override
    {
        executor = std::make_unique<cpp_components::executor::Executor>();
    }

    void TearDown(const ::benchmark::State & /*state*/) override
    {
        if (executor) {
            executor->stop();
            executor.reset();
        }
    }

protected:
    std::unique_ptr<cpp_components::executor::Executor> executor;
};

} // namespace

BENCHMARK_DEFINE_F(PostFixture, post_throughput)(benchmark::State &state)
{
    for (auto _ : state) {
        std::atomic<int> completions { 0 };

        for (int i = 0; i < callback_count; ++i) {
            executor->post(
                [&completions]() { completions.fetch_add(1, std::memory_order_relaxed); });
        }

        const auto deadline = std::chrono::steady_clock::now() + wait_timeout;
        while (completions.load(std::memory_order_relaxed) < callback_count &&
            std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }

        if (completions.load(std::memory_order_relaxed) != callback_count) {
            state.SkipWithError("post completions timed out");
            break;
        }

        state.SetItemsProcessed(callback_count);
    }
}

BENCHMARK_REGISTER_F(PostFixture, post_throughput)->UseRealTime();
