/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "cpp-components/executor/executor.h"
#include "cpp-components/https_client_async/https_client_async.h"

#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>

#include <chrono>
#include <functional>
#include <future>
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

uint16_t closed_port()
{
    boost::asio::io_context ioc;
    boost::asio::ip::tcp::acceptor acceptor(ioc,
        boost::asio::ip::tcp::endpoint(boost::asio::ip::tcp::v4(), 0));
    const auto port = acceptor.local_endpoint().port();
    acceptor.close();
    return port;
}

uint16_t start_plain_tcp_close_server()
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

        // Accept TCP but never speak TLS so the client SSL handshake fails.
        socket.close(ec);
    }).detach();

    return port;
}

uint16_t start_https_server(
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
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor->local_endpoint().port();

    std::thread([ioc, acceptor, handler = std::move(handler)]() {
        ssl::context ssl_context(ssl::context::tlsv12_server);
        ssl_context.use_certificate_chain_file(TEST_CERT_DIR "/test-cert.pem");
        ssl_context.use_private_key_file(TEST_CERT_DIR "/test-key.pem",
            ssl::context::file_format::pem);

        tcp::socket socket(*ioc);
        boost::system::error_code ec;
        acceptor->accept(socket, ec);
        if (ec) {
            return;
        }

        ssl_stream stream(beast::tcp_stream(std::move(socket)), ssl_context);
        stream.handshake(ssl::stream_base::server, ec);
        if (ec) {
            return;
        }

        beast::flat_buffer buffer;
        http::request<http::string_body> request;
        http::read(stream, buffer, request, ec);
        if (ec) {
            return;
        }

        http::response<http::string_body> response { http::status::ok, request.version() };
        response.set(http::field::server, "test");
        response.keep_alive(false);
        handler(request, response);
        response.prepare_payload();
        http::write(stream, response, ec);

        beast::error_code shutdown_ec;
        stream.shutdown(shutdown_ec);
    }).detach();

    return port;
}

} // namespace

TEST(HttpsClientAsyncTest, get_returns_body_and_status)
{
    const auto port = start_https_server([](const auto &request, auto &response) {
        EXPECT_EQ(request.method(), boost::beast::http::verb::get);
        EXPECT_EQ(request.target(), "/hello");
        response.result(boost::beast::http::status::ok);
        response.body() = "world";
    });
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::pair<std::error_code, cpp_components::https_client_async::HttpResponse>>
        result;
    const auto result_future = result.get_future().share();
    client->get("localhost", port_string, "/hello",
        [&result](const std::error_code &ec,
            cpp_components::https_client_async::HttpResponse response) {
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

TEST(HttpsClientAsyncTest, post_sends_body)
{
    const auto port = start_https_server([](const auto &request, auto &response) {
        EXPECT_EQ(request.method(), boost::beast::http::verb::post);
        EXPECT_EQ(request.target(), "/echo");
        EXPECT_EQ(request.body(), "payload");
        response.result(boost::beast::http::status::created);
        response.body() = request.body();
    });
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::pair<std::error_code, cpp_components::https_client_async::HttpResponse>>
        result;
    const auto result_future = result.get_future().share();
    client->post("localhost", port_string, "/echo", "payload",
        [&result](const std::error_code &ec,
            cpp_components::https_client_async::HttpResponse response) {
            result.set_value({ ec, std::move(response) });
        });

    ASSERT_TRUE(wait_ready(result_future));
    const auto [ec, response] = result_future.get();
    EXPECT_FALSE(ec);
    EXPECT_EQ(response.status_code, 201u);
    EXPECT_EQ(response.body, "payload");
    executor.stop();
}

TEST(HttpsClientAsyncTest, custom_headers_are_sent)
{
    const auto port = start_https_server([](const auto &request, auto &response) {
        EXPECT_EQ(request["X-Test"], "value");
        response.result(boost::beast::http::status::no_content);
        response.body().clear();
    });
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->request(cpp_components::https_client_async::HttpMethod::get, "localhost", port_string,
        "/",
        {
    },
        { { "X-Test", "value" } },
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_FALSE(result_future.get());
    executor.stop();
}

TEST(HttpsClientAsyncTest, response_headers_are_returned)
{
    const auto port = start_https_server([](const auto &, auto &response) {
        response.set("X-Reply", "yes");
        response.body() = "ok";
    });
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<cpp_components::https_client_async::HttpResponse> result;
    const auto result_future = result.get_future().share();
    client->get("localhost", port_string, "/",
        [&result](const std::error_code &ec,
            cpp_components::https_client_async::HttpResponse response) {
            if (!ec) {
                result.set_value(std::move(response));
            }
        });

    ASSERT_TRUE(wait_ready(result_future));
    const auto response = result_future.get();
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

TEST(HttpsClientAsyncTest, second_request_while_busy_fails)
{
    const auto port = start_https_server([](const auto &, auto &response) {
        std::this_thread::sleep_for(std::chrono::milliseconds(200));
        response.body() = "slow";
    });
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::error_code> first_result;
    const auto first_future = first_result.get_future().share();
    client->get("localhost", port_string, "/",
        [&first_result](const std::error_code &ec, const auto &) { first_result.set_value(ec); });

    std::promise<std::error_code> second_result;
    const auto second_future = second_result.get_future().share();
    client->get("localhost", port_string, "/",
        [&second_result](const std::error_code &ec, const auto &) { second_result.set_value(ec); });

    ASSERT_TRUE(wait_ready(second_future));
    EXPECT_EQ(second_future.get(), std::make_error_code(std::errc::operation_in_progress));

    ASSERT_TRUE(wait_ready(first_future));
    EXPECT_FALSE(first_future.get());
    executor.stop();
}

TEST(HttpsClientAsyncTest, cancel_aborts_in_flight_request)
{
    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

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

TEST(HttpsClientAsyncTest, connect_reports_dns_failure)
{
    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("nonexistent.invalid", "443", "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_TRUE(result_future.get());
    EXPECT_FALSE(client->is_busy());
    executor.stop();
}

TEST(HttpsClientAsyncTest, connect_reports_connection_refused)
{
    const auto port_string = std::to_string(closed_port());

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("127.0.0.1", port_string, "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_TRUE(result_future.get());
    EXPECT_FALSE(client->is_busy());
    executor.stop();
}

TEST(HttpsClientAsyncTest, destroy_while_busy_cleans_up)
{
    auto executor = std::make_unique<cpp_components::executor::Executor>();
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(*executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    client->get("192.0.2.1", "9", "/", [](const std::error_code &, const auto &) {});

    const std::weak_ptr<cpp_components::https_client_async::HttpsClientAsync> weak_client = client;
    client.reset();
    executor.reset();
    EXPECT_TRUE(weak_client.expired());
}

TEST(HttpsClientAsyncTest, set_timeout_allows_successful_request)
{
    const auto port = start_https_server([](const auto &, auto &response) {
        response.body() = "ok";
    });
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
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

TEST(HttpsClientAsyncTest, set_timeout_zero_disables_deadline)
{
    const auto port = start_https_server([](const auto &, auto &response) {
        response.body() = "ok";
    });
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
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

TEST(HttpsClientAsyncTest, empty_ca_certificate_is_ignored)
{
    const auto port = start_https_server([](const auto &, auto &response) {
        response.body() = "ok";
    });
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
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

TEST(HttpsClientAsyncTest, request_supports_remaining_http_methods)
{
    using cpp_components::https_client_async::HttpMethod;

    const struct {
        HttpMethod method;
        boost::beast::http::verb expected;
    } cases[] = {
        { HttpMethod::head, boost::beast::http::verb::head },
        { HttpMethod::put, boost::beast::http::verb::put },
        { HttpMethod::del, boost::beast::http::verb::delete_ },
        { HttpMethod::patch, boost::beast::http::verb::patch },
    };

    for (const auto &test_case : cases) {
        const auto port = start_https_server(
            [expected = test_case.expected](const auto &request, auto &response) {
                EXPECT_EQ(request.method(), expected);
                response.result(boost::beast::http::status::no_content);
                response.body().clear();
            });
        const auto port_string = std::to_string(port);

        cpp_components::executor::Executor executor {};
        auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
        client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

        std::promise<std::error_code> result;
        const auto result_future = result.get_future().share();
        client->request(test_case.method, "localhost", port_string, "/", {}, {},
            [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

        ASSERT_TRUE(wait_ready(result_future));
        EXPECT_FALSE(result_future.get());
        executor.stop();
    }
}

TEST(HttpsClientAsyncTest, ssl_handshake_failure_is_reported)
{
    const auto port = start_plain_tcp_close_server();
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::https_client_async::HttpsClientAsync::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::error_code> result;
    const auto result_future = result.get_future().share();
    client->get("localhost", port_string, "/",
        [&result](const std::error_code &ec, const auto &) { result.set_value(ec); });

    ASSERT_TRUE(wait_ready(result_future));
    EXPECT_TRUE(result_future.get());
    EXPECT_FALSE(client->is_busy());
    executor.stop();
}

