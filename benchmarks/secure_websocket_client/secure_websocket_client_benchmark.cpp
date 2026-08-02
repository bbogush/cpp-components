/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "cpp_components/executor/executor.h"
#include "cpp_components/secure_websocket_client/secure_websocket_client.h"

#include <benchmark/benchmark.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <chrono>
#include <condition_variable>
#include <future>
#include <memory>
#include <mutex>
#include <string>
#include <system_error>
#include <thread>

namespace {

constexpr auto wait_timeout = std::chrono::seconds { 5 };

template<typename T>
bool wait_ready(const std::shared_future<T> &future)
{
    return future.wait_for(wait_timeout) == std::future_status::ready;
}

void run_secure_echo_session(boost::asio::ip::tcp::socket socket)
{
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    namespace ssl = boost::asio::ssl;
    using websocket_stream = websocket::stream<ssl::stream<beast::tcp_stream>>;

    ssl::context ssl_context(ssl::context::tlsv12_server);
    ssl_context.use_certificate_chain_file(BENCHMARK_CERT_DIR "/test-cert.pem");
    ssl_context.use_private_key_file(BENCHMARK_CERT_DIR "/test-key.pem",
        ssl::context::file_format::pem);

    boost::system::error_code ec;
    websocket_stream ws(ssl::stream<beast::tcp_stream>(std::move(socket), ssl_context));
    ws.next_layer().handshake(ssl::stream_base::server, ec);
    if (ec) {
        return;
    }

    ws.accept(ec);
    if (ec) {
        return;
    }

    for (;;) {
        beast::flat_buffer buffer;
        ws.read(buffer, ec);
        if (ec) {
            break;
        }

        ws.write(buffer.data(), ec);
        if (ec) {
            break;
        }
        buffer.consume(buffer.size());
    }
}

uint16_t start_secure_echo_server()
{
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    auto ioc = std::make_shared<net::io_context>();
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor->local_endpoint().port();

    std::thread([ioc, acceptor]() {
        tcp::socket socket(*ioc);
        boost::system::error_code ec;
        acceptor->accept(socket, ec);
        if (ec) {
            return;
        }
        run_secure_echo_session(std::move(socket));
    }).detach();

    return port;
}

class WriteReceiveFixture : public benchmark::Fixture
{
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

        std::promise<void> connected;
        const auto connected_future = connected.get_future().share();
        client->connect("localhost", port_string, "/", [&connected](const std::error_code &ec) {
            if (!ec) {
                connected.set_value();
            }
        });

        if (!wait_ready(connected_future) || !client->is_connected()) {
            setup_error = "failed to connect";
        }
    }

    void TearDown(const ::benchmark::State & /*state*/) override
    {
        if (client && client->is_connected()) {
            std::promise<void> closed;
            const auto closed_future = closed.get_future().share();
            client->close([&closed](const std::error_code &ec) {
                if (!ec) {
                    closed.set_value();
                }
            });
            wait_ready(closed_future);
        }

        client.reset();
        if (executor) {
            executor->stop();
            executor.reset();
        }
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

BENCHMARK_DEFINE_F(WriteReceiveFixture, write_and_receive)(benchmark::State &state)
{
    if (!setup_error.empty()) {
        state.SkipWithError(setup_error.c_str());
        return;
    }

    const std::string payload = "ping";

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

BENCHMARK_REGISTER_F(WriteReceiveFixture, write_and_receive)->Unit(benchmark::kMicrosecond);
