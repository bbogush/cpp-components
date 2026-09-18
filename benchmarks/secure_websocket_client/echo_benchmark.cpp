/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "benchmark_support.h"

#include "cpp_components/executor/executor.h"
#include "cpp_components/secure_websocket_client/secure_websocket_client.h"

#include <benchmark/benchmark.h>

#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>

namespace {

using namespace secure_websocket_client_benchmark;

class EchoFixture : public benchmark::Fixture {
public:
    void SetUp(const ::benchmark::State & /*state*/) override
    {
        const auto port = start_secure_echo_server();
        port_string = std::to_string(port);

        executor = std::make_unique<cpp_components::executor::Executor>();
        client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(*executor);
        client->set_ca_certificate(BENCHMARK_CERT_DIR "/test-cert.pem");

        client->set_message_handler([this](const char *data, size_t size) {
            std::lock_guard<std::mutex> lock(mutex);
            received.assign(data, size);
            message_ready = true;
            cv.notify_one();
        });

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
    std::mutex mutex;
    std::condition_variable cv;
    std::string received;
    bool message_ready = false;
};

} // namespace

BENCHMARK_DEFINE_F(EchoFixture, write_and_receive)(benchmark::State &state)
{
    if (!setup_error.empty()) {
        state.SkipWithError(setup_error.c_str());
        return;
    }

    const std::string payload(benchmark_payload);

    for (auto _ : state) {
        {
            std::lock_guard<std::mutex> lock(mutex);
            message_ready = false;
            received.clear();
        }

        std::promise<void> written;
        const auto written_future = written.get_future().share();
        client->write(payload, [&written](const std::error_code &ec) {
            if (!ec) {
                written.set_value();
            }
        });

        if (!wait_ready(written_future)) {
            state.SkipWithError("write timed out");
            break;
        }

        {
            std::unique_lock<std::mutex> lock(mutex);
            if (!cv.wait_for(lock, wait_timeout, [this]() { return message_ready; })) {
                state.SkipWithError("receive timed out");
                break;
            }
            if (received != payload) {
                state.SkipWithError("echo payload mismatch");
                break;
            }
        }
    }
}

BENCHMARK_REGISTER_F(EchoFixture, write_and_receive)->Unit(benchmark::kMicrosecond);
