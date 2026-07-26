/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "cpp-components/executor/executor.h"
#include "cpp-components/https_multiplex_client/https_multiplex_client.h"

#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <openssl/crypto.h>

#include <atomic>
#include <chrono>
#include <functional>
#include <future>
#include <string>
#include <system_error>
#include <thread>
#include <vector>

namespace {

constexpr auto wait_timeout = std::chrono::seconds { 5 };

template<typename T>
bool wait_ready(const std::shared_future<T> &future)
{
    return future.wait_for(wait_timeout) == std::future_status::ready;
}

bool wait_until_busy(const cpp_components::https_multiplex_client::HttpsMultiplexClient &client)
{
    const auto deadline = std::chrono::steady_clock::now() + wait_timeout;
    while (std::chrono::steady_clock::now() < deadline) {
        if (client.is_busy()) {
            return true;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds { 5 });
    }
    return client.is_busy();
}

uint16_t closed_port()
{
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::acceptor acceptor(ioc,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    const auto port = acceptor.local_endpoint().port();
    acceptor.close();
    return port;
}

struct HttpsServer {
    uint16_t port = 0;
    std::thread thread;
    std::shared_ptr<std::atomic<bool>> stop;

    HttpsServer() = default;
    HttpsServer(const HttpsServer &) = delete;
    HttpsServer &operator=(const HttpsServer &) = delete;
    HttpsServer(HttpsServer &&other) noexcept
        : port(other.port), thread(std::move(other.thread)), stop(std::move(other.stop))
    {
        other.port = 0;
    }
    HttpsServer &operator=(HttpsServer &&other) noexcept
    {
        if (this != &other) {
            if (stop) {
                stop->store(true, std::memory_order_release);
            }
            if (thread.joinable()) {
                thread.join();
            }
            port = other.port;
            thread = std::move(other.thread);
            stop = std::move(other.stop);
            other.port = 0;
        }
        return *this;
    }
    ~HttpsServer()
    {
        if (stop) {
            stop->store(true, std::memory_order_release);
        }
        if (thread.joinable()) {
            thread.join();
        }
    }
};

HttpsServer start_https_server(std::size_t max_connections,
    std::function<void(const boost::beast::http::request<boost::beast::http::string_body> &,
        boost::beast::http::response<boost::beast::http::string_body> &)>
        handler)
{
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace net = boost::asio;
    namespace ssl = net::ssl;
    using tcp = net::ip::tcp;
    using ssl_stream = ssl::stream<beast::tcp_stream>;

    auto ioc = std::make_shared<net::io_context>();
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc);
    acceptor->open(tcp::v6());
    acceptor->set_option(net::ip::v6_only(false));
    acceptor->bind(tcp::endpoint(tcp::v6(), 0));
    acceptor->listen();

    HttpsServer server;
    server.port = acceptor->local_endpoint().port();
    server.thread = std::thread([ioc, acceptor, handler = std::move(handler), max_connections]() {
        ssl::context ssl_context(ssl::context::tlsv12_server);
        ssl_context.use_certificate_chain_file(TEST_CERT_DIR "/test-cert.pem");
        ssl_context.use_private_key_file(TEST_CERT_DIR "/test-key.pem",
            ssl::context::file_format::pem);

        std::vector<std::thread> workers;
        workers.reserve(max_connections);

        for (std::size_t i = 0; i < max_connections; ++i) {
            tcp::socket socket(*ioc);
            boost::system::error_code ec;
            acceptor->accept(socket, ec);
            if (ec) {
                break;
            }

            workers.emplace_back([socket = std::move(socket), &ssl_context, handler]() mutable {
                boost::system::error_code ec;
                ssl_stream stream(beast::tcp_stream(std::move(socket)), ssl_context);
                stream.handshake(ssl::stream_base::server, ec);
                if (ec) {
                    // Ensure the current thread leaves no OpenSSL thread-local state behind
                    // It is reported as a leak by sanitizers.
                    OPENSSL_thread_stop();
                    return;
                }

                for (;;) {
                    beast::flat_buffer buffer;
                    http::request<http::string_body> request;
                    http::read(stream, buffer, request, ec);
                    if (ec) {
                        break;
                    }

                    http::response<http::string_body> response { http::status::ok,
                        request.version() };
                    response.set(http::field::server, "test");
                    response.keep_alive(false);
                    handler(request, response);
                    response.prepare_payload();
                    http::write(stream, response, ec);
                    if (ec || !response.keep_alive()) {
                        break;
                    }
                }

                beast::error_code shutdown_ec;
                stream.shutdown(shutdown_ec);
                OPENSSL_thread_stop();
            });
        }

        for (auto &worker : workers) {
            worker.join();
        }
        OPENSSL_thread_stop();
    });

    return server;
}

// Accepts TLS then stalls until destroyed so the client keeps an open socket.
HttpsServer start_hanging_https_server()
{
    namespace beast = boost::beast;
    namespace net = boost::asio;
    namespace ssl = net::ssl;
    using tcp = net::ip::tcp;
    using ssl_stream = ssl::stream<beast::tcp_stream>;

    auto ioc = std::make_shared<net::io_context>();
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc);
    acceptor->open(tcp::v6());
    acceptor->set_option(net::ip::v6_only(false));
    acceptor->bind(tcp::endpoint(tcp::v6(), 0));
    acceptor->listen();

    HttpsServer server;
    server.port = acceptor->local_endpoint().port();
    server.stop = std::make_shared<std::atomic<bool>>(false);
    server.thread = std::thread([ioc, acceptor, stop = server.stop]() {
        ssl::context ssl_context(ssl::context::tlsv12_server);
        ssl_context.use_certificate_chain_file(TEST_CERT_DIR "/test-cert.pem");
        ssl_context.use_private_key_file(TEST_CERT_DIR "/test-key.pem",
            ssl::context::file_format::pem);

        tcp::socket socket(*ioc);
        boost::system::error_code ec;
        acceptor->accept(socket, ec);
        if (ec) {
            OPENSSL_thread_stop();
            return;
        }

        ssl_stream stream(beast::tcp_stream(std::move(socket)), ssl_context);
        stream.handshake(ssl::stream_base::server, ec);
        while (!ec && !stop->load(std::memory_order_acquire)) {
            std::this_thread::sleep_for(std::chrono::milliseconds { 10 });
        }

        OPENSSL_thread_stop();
    });

    return server;
}

} // namespace

TEST(HttpsMultiplexClientTest, get_returns_body_and_status)
{
    auto server = start_https_server(1, [](const auto &request, auto &response) {
        EXPECT_EQ(request.method(), boost::beast::http::verb::get);
        EXPECT_EQ(request.target(), "/hello");
        response.result(boost::beast::http::status::ok);
        response.body() = "world";
    });
    const auto port_string = std::to_string(server.port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::pair<std::error_code, cpp_components::https_multiplex_client::HttpResponse>>
        result;
    const auto result_future = result.get_future().share();
    client->get("localhost", port_string, "/hello",
        [&result](const std::error_code &ec,
            cpp_components::https_multiplex_client::HttpResponse response) {
            result.set_value({ ec, std::move(response) });
        });

    ASSERT_TRUE(wait_ready(result_future));
    const auto [ec, response] = result_future.get();
    EXPECT_FALSE(ec);
    EXPECT_EQ(response.status_code, 200u);
    EXPECT_EQ(response.body, "world");
    EXPECT_FALSE(client->is_busy());
    executor.stop();
}

TEST(HttpsMultiplexClientTest, post_sends_body_and_headers)
{
    auto server = start_https_server(1, [](const auto &request, auto &response) {
        EXPECT_EQ(request.method(), boost::beast::http::verb::post);
        EXPECT_EQ(request.target(), "/echo?q=1");
        EXPECT_EQ(request.body(), "payload");
        EXPECT_EQ(request["X-Test"], "value");
        response.result(boost::beast::http::status::created);
        response.set("X-Reply", "yes");
        response.body() = request.body();
    });
    const auto port_string = std::to_string(server.port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::pair<std::error_code, cpp_components::https_multiplex_client::HttpResponse>>
        result;
    const auto result_future = result.get_future().share();
    client->request(cpp_components::https_multiplex_client::HttpMethod::post, "localhost",
        port_string, "/echo?q=1", "payload",
        {
            { "X-Test", "value" }
    },
        [&result](const std::error_code &ec,
            cpp_components::https_multiplex_client::HttpResponse response) {
            result.set_value({ ec, std::move(response) });
        });

    ASSERT_TRUE(wait_ready(result_future));
    const auto [ec, response] = result_future.get();
    EXPECT_FALSE(ec);
    EXPECT_EQ(response.status_code, 201u);
    EXPECT_EQ(response.body, "payload");
    bool found = false;
    for (const auto &header : response.headers) {
        if (header.name == "X-Reply" && header.value == "yes") {
            found = true;
            break;
        }
    }
    EXPECT_TRUE(found);
    executor.stop();
}

TEST(HttpsMultiplexClientTest, concurrent_requests_complete)
{
    constexpr int request_count = 4;
    auto server = start_https_server(static_cast<std::size_t>(request_count),
        [](const auto &request, auto &response) {
            response.result(boost::beast::http::status::ok);
            response.body() = std::string(request.target());
        });
    const auto port_string = std::to_string(server.port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::vector<std::promise<std::pair<std::error_code, std::string>>> results(request_count);
    std::vector<std::shared_future<std::pair<std::error_code, std::string>>> futures;
    futures.reserve(request_count);

    for (int i = 0; i < request_count; ++i) {
        futures.push_back(results[static_cast<std::size_t>(i)].get_future().share());
        const auto target = "/item/" + std::to_string(i);
        client->get("localhost", port_string, target,
            [&results, i](const std::error_code &ec,
                cpp_components::https_multiplex_client::HttpResponse response) {
                results[static_cast<std::size_t>(i)].set_value({ ec, std::move(response.body) });
            });
    }

    for (int i = 0; i < request_count; ++i) {
        ASSERT_TRUE(wait_ready(futures[static_cast<std::size_t>(i)]));
        const auto [ec, body] = futures[static_cast<std::size_t>(i)].get();
        EXPECT_FALSE(ec);
        EXPECT_EQ(body, "/item/" + std::to_string(i));
    }
    EXPECT_FALSE(client->is_busy());
    EXPECT_EQ(client->pending_count(), 0u);
    executor.stop();
}

TEST(HttpsMultiplexClientTest, unsupported_method_fails)
{
    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->request(static_cast<cpp_components::https_multiplex_client::HttpMethod>(99),
        "localhost", "443", "/", {}, {},
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_EQ(result_future.get(), std::make_error_code(std::errc::invalid_argument));
    executor.stop();
}

TEST(HttpsMultiplexClientTest, cancel_aborts_in_flight_request)
{
    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_timeout(std::chrono::seconds { 5 });

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("192.0.2.1", "9", "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    client->cancel();

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_EQ(result_future.get(), std::make_error_code(std::errc::operation_canceled));
    EXPECT_FALSE(client->is_busy());
    executor.stop();
}

TEST(HttpsMultiplexClientTest, cancel_closes_active_sockets)
{
    auto server = start_hanging_https_server();
    const auto port_string = std::to_string(server.port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_timeout(std::chrono::seconds { 15 });

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("localhost", port_string, "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_until_busy(*client));
    // Give curl time to open the TCP socket and arm Asio watches.
    std::this_thread::sleep_for(std::chrono::milliseconds { 100 });
    client->cancel();

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_EQ(result_future.get(), std::make_error_code(std::errc::operation_canceled));
    EXPECT_FALSE(client->is_busy());
    executor.stop();
}

TEST(HttpsMultiplexClientTest, dns_failure_is_reported)
{
    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_timeout(std::chrono::seconds { 3 });

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("nonexistent.invalid", "443", "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_TRUE(result_future.get());
    EXPECT_FALSE(client->is_busy());
    executor.stop();
}

TEST(HttpsMultiplexClientTest, connection_refused_is_reported)
{
    const auto port_string = std::to_string(closed_port());

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_timeout(std::chrono::seconds { 3 });

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("127.0.0.1", port_string, "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    const auto ec = result_future.get();
    EXPECT_TRUE(ec);
    EXPECT_TRUE(ec == std::errc::connection_refused || ec == std::errc::io_error ||
        ec == std::errc::host_unreachable);
    EXPECT_FALSE(client->is_busy());
    executor.stop();
}

TEST(HttpsMultiplexClientTest, request_timeout_is_reported)
{
    auto server = start_hanging_https_server();
    const auto port_string = std::to_string(server.port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_timeout(std::chrono::seconds { 1 });

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("localhost", port_string, "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_EQ(result_future.get(), std::make_error_code(std::errc::timed_out));
    EXPECT_FALSE(client->is_busy());
    executor.stop();
}

TEST(HttpsMultiplexClientTest, destroy_while_busy_cleans_up)
{
    auto server = start_hanging_https_server();
    const auto port_string = std::to_string(server.port);

    auto executor = std::make_unique<cpp_components::executor::Executor>();
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(*executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_timeout(std::chrono::seconds { 15 });

    client->get("localhost", port_string, "/", [](const std::error_code &, const auto &) {});
    ASSERT_TRUE(wait_until_busy(*client));
    std::this_thread::sleep_for(std::chrono::milliseconds { 100 });

    const std::weak_ptr<cpp_components::https_multiplex_client::HttpsMultiplexClient> weak_client =
        client;
    client.reset();
    executor.reset();
    EXPECT_TRUE(weak_client.expired());
}

TEST(HttpsMultiplexClientTest, empty_ca_certificate_is_ignored)
{
    auto server = start_https_server(1,
        [](const auto &, auto &response) { response.body() = "ok"; });
    const auto port_string = std::to_string(server.port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate({});
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("localhost", port_string, "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_FALSE(result_future.get());
    executor.stop();
}

TEST(HttpsMultiplexClientTest, set_timeout_allows_successful_request)
{
    auto server = start_https_server(1,
        [](const auto &, auto &response) { response.body() = "ok"; });
    const auto port_string = std::to_string(server.port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_timeout(std::chrono::seconds { 5 });

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("localhost", port_string, "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_FALSE(result_future.get());
    executor.stop();
}

TEST(HttpsMultiplexClientTest, remaining_http_methods_are_supported)
{
    using cpp_components::https_multiplex_client::HttpMethod;

    const struct {
        HttpMethod method;
        boost::beast::http::verb expected;
    } cases[] = {
        { HttpMethod::head,  boost::beast::http::verb::head    },
        { HttpMethod::put,   boost::beast::http::verb::put     },
        { HttpMethod::del,   boost::beast::http::verb::delete_ },
        { HttpMethod::patch, boost::beast::http::verb::patch   },
    };

    for (const auto &test_case : cases) {
        auto server = start_https_server(1,
            [expected = test_case.expected](const auto &request, auto &response) {
                EXPECT_EQ(request.method(), expected);
                response.result(boost::beast::http::status::no_content);
                response.body().clear();
            });
        const auto port_string = std::to_string(server.port);

        cpp_components::executor::Executor executor {};
        auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(
            executor);
        client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

        std::promise<std::error_code> result;
        const auto result_future = result.get_future().share();
        client->request(test_case.method, "localhost", port_string, "/", "body", {},
            [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

        ASSERT_TRUE(wait_ready(result_future));
        EXPECT_FALSE(result_future.get());
        executor.stop();
    }
}

TEST(HttpsMultiplexClientTest, zero_timeout_still_allows_request)
{
    auto server = start_https_server(1,
        [](const auto &, auto &response) { response.body() = "ok"; });
    const auto port_string = std::to_string(server.port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_timeout(std::chrono::seconds::zero());

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("localhost", port_string, "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_FALSE(result_future.get());
    executor.stop();
}

TEST(HttpsMultiplexClientTest, post_helper_sends_body)
{
    auto server = start_https_server(1, [](const auto &request, auto &response) {
        EXPECT_EQ(request.method(), boost::beast::http::verb::post);
        EXPECT_EQ(request.body(), "payload");
        response.result(boost::beast::http::status::created);
        response.body() = request.body();
    });
    const auto port_string = std::to_string(server.port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_multiplex_client::HttpsMultiplexClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::pair<std::error_code, std::string>> result;
    const auto result_future = result.get_future().share();
    client->post("localhost", port_string, "/echo", "payload",
        [&result](const std::error_code &ec,
            cpp_components::https_multiplex_client::HttpResponse response) {
            result.set_value({ ec, std::move(response.body) });
        });

    ASSERT_TRUE(wait_ready(result_future));
    const auto [ec, body] = result_future.get();
    EXPECT_FALSE(ec);
    EXPECT_EQ(body, "payload");
    executor.stop();
}
