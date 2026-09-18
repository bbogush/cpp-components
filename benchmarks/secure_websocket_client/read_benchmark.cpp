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

constexpr auto throughput_sample_time = std::chrono::milliseconds { 2000 };

class ReadFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State & /*state*/) override
    {
        const auto port = start_secure_push_server();
        port_string = std::to_string(port);

        executor = std::make_unique<cpp_components::executor::Executor>();
        client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(*executor);
        client->set_ca_certificate(BENCHMARK_CERT_DIR "/test-cert.pem");
        client->set_message_handler([this](const char *, size_t) {
            if (measuring) {
                messages_received.fetch_add(1, std::memory_order_relaxed);
            }
        });

        if (!connect_client(*client, port_string)) {
            setup_error = "failed to connect";
        }
    }

    void TearDown(const ::benchmark::State & /*state*/) override
    {
        measuring = false;

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
    std::atomic<bool> measuring { false };
    std::atomic<int64_t> messages_received { 0 };
};

} // namespace

BENCHMARK_DEFINE_F(ReadFixture, read_throughput)(benchmark::State &state)
{
    if (!setup_error.empty()) {
        state.SkipWithError(setup_error.c_str());
        return;
    }

    for (auto _ : state) {
        messages_received = 0;
        measuring = true;

        std::this_thread::sleep_for(throughput_sample_time);

        measuring = false;
        state.SetItemsProcessed(messages_received.load(std::memory_order_relaxed));
    }
}

BENCHMARK_REGISTER_F(ReadFixture, read_throughput)->UseRealTime();
