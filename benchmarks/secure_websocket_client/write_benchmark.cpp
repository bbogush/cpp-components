/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "benchmark_support.h"

#include "cpp_components/executor/executor.h"
#include "cpp_components/secure_websocket_client/secure_websocket_client.h"

#include <benchmark/benchmark.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>

namespace {

using namespace secure_websocket_client_benchmark;

constexpr int message_count = 100000;

class WriteFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State & /*state*/) override
    {
        const auto port = start_secure_echo_server();
        port_string = std::to_string(port);

        executor = std::make_unique<cpp_components::executor::Executor>();
        client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(*executor);
        client->set_ca_certificate(BENCHMARK_CERT_DIR "/test-cert.pem");
        client->set_message_handler([](const char *, size_t) {});

        if (!connect_client(*client, port_string)) {
            setup_error = "failed to connect";
        }
    }

    void TearDown(const ::benchmark::State & /*state*/) override
    {
        if (client) {
            close_client(*client);
        }

        if (executor) {
            executor->stop(true);
        }
        client.reset();
        executor.reset();
    }

protected:
    std::string setup_error;
    std::string port_string;
    std::unique_ptr<cpp_components::executor::Executor> executor;
    std::shared_ptr<cpp_components::secure_websocket_client::SecureWebSocketClient> client;
};

} // namespace

BENCHMARK_DEFINE_F(WriteFixture, write_throughput)(benchmark::State &state)
{
    if (!setup_error.empty()) {
        state.SkipWithError(setup_error.c_str());
        return;
    }

    const std::string payload(benchmark_payload);

    for (auto _ : state) {
        std::atomic<int> completions { 0 };
        std::atomic<bool> failed { false };

        for (int i = 0; i < message_count; ++i) {
            client->write(payload, [&completions, &failed](const std::error_code &ec) {
                if (ec) {
                    failed = true;
                    return;
                }
                completions.fetch_add(1, std::memory_order_relaxed);
            });
        }

        const auto deadline = std::chrono::steady_clock::now() + wait_timeout;
        while (completions.load(std::memory_order_relaxed) < message_count && !failed.load() &&
            std::chrono::steady_clock::now() < deadline) {
            std::this_thread::yield();
        }

        if (failed.load()) {
            state.SkipWithError("write failed");
            break;
        }

        if (completions.load(std::memory_order_relaxed) != message_count) {
            state.SkipWithError("write completions timed out");
            break;
        }

        state.SetItemsProcessed(message_count);
    }
}

BENCHMARK_REGISTER_F(WriteFixture, write_throughput)->UseRealTime();
