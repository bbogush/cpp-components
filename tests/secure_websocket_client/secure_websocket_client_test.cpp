/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "executor/executor.h"
#include "secure_websocket_client/secure_websocket_client.h"

#include <gtest/gtest.h>

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <atomic>
#include <chrono>
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

uint16_t start_secure_echo_server(bool close_after_first_message = false)
{
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    namespace net = boost::asio;
    namespace ssl = net::ssl;
    using tcp = net::ip::tcp;
    using websocket_stream = websocket::stream<ssl::stream<beast::tcp_stream>>;

    auto ioc = std::make_shared<net::io_context>();
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor->local_endpoint().port();

    std::thread([ioc, acceptor, close_after_first_message]() {
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

            if (close_after_first_message) {
                break;
            }
        }
    }).detach();

    return port;
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

uint16_t start_secure_websocket_path_server(std::string allowed_resource)
{
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace websocket = beast::websocket;
    namespace net = boost::asio;
    namespace ssl = net::ssl;
    using tcp = net::ip::tcp;
    using ssl_stream = ssl::stream<beast::tcp_stream>;

    auto ioc = std::make_shared<net::io_context>();
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor->local_endpoint().port();

    std::thread([ioc, acceptor, allowed_resource = std::move(allowed_resource)]() {
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

        if (request.target() != allowed_resource) {
            http::response<http::string_body> response { http::status::not_found,
                request.version() };
            response.set(http::field::server, "test");
            response.set(http::field::connection, "close");
            response.keep_alive(false);
            response.body() = "Not Found";
            response.prepare_payload();
            http::write(stream, response, ec);
            beast::error_code shutdown_ec;
            stream.shutdown(shutdown_ec);
            return;
        }

        websocket::stream<ssl_stream> ws(std::move(stream));
        ws.accept(request, ec);
    }).detach();

    return port;
}

uint16_t start_secure_ping_pong_server(std::string expected_ping)
{
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    namespace net = boost::asio;
    namespace ssl = net::ssl;
    using tcp = net::ip::tcp;
    using websocket_stream = websocket::stream<ssl::stream<beast::tcp_stream>>;

    auto ioc = std::make_shared<net::io_context>();
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor->local_endpoint().port();

    std::thread([ioc, acceptor, expected_ping = std::move(expected_ping)]() {
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

            const auto data = buffer.cdata();
            const auto message =
                std::string(static_cast<const char *>(data.data()), data.size());
            if (message == expected_ping) {
                ws.write(net::buffer(std::string("pong")), ec);
            }
            if (ec) {
                break;
            }
        }
    }).detach();

    return port;
}

} // namespace

TEST(SecureWebSocketClientTest, connect_write_and_receive)
{
    const auto port = start_secure_echo_server();
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<void> connected;
    const auto connected_future = connected.get_future().share();
    client->connect("localhost", port_string, "/", [&connected](const std::error_code &ec) {
        if (!ec) {
            connected.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(connected_future));
    EXPECT_TRUE(client->is_connected());

    std::promise<std::string> message_received;
    const auto message_received_future = message_received.get_future().share();
    client->set_message_handler([&message_received](const char *data, size_t size) {
        message_received.set_value(std::string(data, size));
    });

    std::promise<void> message_written;
    const auto message_written_future = message_written.get_future().share();
    client->write("ping", [&message_written](const std::error_code &ec) {
        if (!ec) {
            message_written.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(message_written_future));
    ASSERT_TRUE(wait_ready(message_received_future));
    EXPECT_EQ(message_received_future.get(), "ping");

    std::promise<void> closed;
    const auto closed_future = closed.get_future().share();
    client->close([&closed](const std::error_code &ec) {
        if (!ec) {
            closed.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(closed_future));
    EXPECT_FALSE(client->is_connected());
    executor.stop();
}

TEST(SecureWebSocketClientTest, destroy_while_connected_cleans_up)
{
    const auto port = start_secure_echo_server();
    const auto port_string = std::to_string(port);

    auto executor = std::make_unique<cpp_components::executor::Executor>();
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(*executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<void> connected;
    const auto connected_future = connected.get_future().share();
    client->connect("localhost", port_string, "/", [&connected](const std::error_code &ec) {
        if (!ec) {
            connected.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(connected_future));
    EXPECT_TRUE(client->is_connected());

    // Drop the external reference without close(). Destroying the executor abandons
    // pending async handlers (which hold shared_from_this), so the client destructor
    // runs while still connected and performs cleanup.
    const std::weak_ptr<cpp_components::secure_websocket_client::SecureWebSocketClient>
        weak_client = client;
    client.reset();
    executor.reset();
    EXPECT_TRUE(weak_client.expired());
}

TEST(SecureWebSocketClientTest, disconnect_handler_called_on_read_error)
{
    const auto port = start_secure_echo_server(true);
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<void> connected;
    const auto connected_future = connected.get_future().share();
    client->connect("localhost", port_string, "/", [&connected](const std::error_code &ec) {
        if (!ec) {
            connected.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(connected_future));

    std::promise<std::error_code> disconnected;
    const auto disconnected_future = disconnected.get_future().share();
    client->set_disconnect_handler(
        [&disconnected](const std::error_code &ec) { disconnected.set_value(ec); });

    std::promise<void> message_written;
    const auto message_written_future = message_written.get_future().share();
    client->write("ping", [&message_written](const std::error_code &ec) {
        if (!ec) {
            message_written.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(message_written_future));
    ASSERT_TRUE(wait_ready(disconnected_future));
    EXPECT_TRUE(disconnected_future.get());
    EXPECT_FALSE(client->is_connected());
    executor.stop();
}

TEST(SecureWebSocketClientTest, close_during_connect_completes_without_connect_handler)
{
    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::atomic<bool> connect_handler_called { false };
    client->connect("192.0.2.1", "9", "/",
        [&connect_handler_called](const std::error_code &) { connect_handler_called.store(true); });

    std::promise<void> closed;
    const auto closed_future = closed.get_future().share();
    client->close([&closed](const std::error_code &ec) {
        if (!ec) {
            closed.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(closed_future));
    EXPECT_FALSE(client->is_connected());
    EXPECT_FALSE(connect_handler_called.load());
    executor.stop();
}

TEST(SecureWebSocketClientTest, connect_reports_dns_failure)
{
    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);

    std::promise<std::error_code> connect_result;
    const auto connect_result_future = connect_result.get_future().share();
    client->connect("nonexistent.invalid", "443", "/",
        [&connect_result](const std::error_code &ec) { connect_result.set_value(ec); });

    ASSERT_TRUE(wait_ready(connect_result_future));
    EXPECT_TRUE(connect_result_future.get());
    EXPECT_FALSE(client->is_connected());
    executor.stop();
}

TEST(SecureWebSocketClientTest, connect_reports_connection_refused)
{
    const auto port_string = std::to_string(closed_port());

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);

    std::promise<std::error_code> connect_result;
    const auto connect_result_future = connect_result.get_future().share();
    client->connect("127.0.0.1", port_string, "/",
        [&connect_result](const std::error_code &ec) { connect_result.set_value(ec); });

    ASSERT_TRUE(wait_ready(connect_result_future));
    EXPECT_EQ(connect_result_future.get(), std::errc::connection_refused);
    EXPECT_FALSE(client->is_connected());
    executor.stop();
}

TEST(SecureWebSocketClientTest, connect_reports_tls_handshake_failure)
{
    const auto port = start_plain_tcp_close_server();
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::error_code> connect_result;
    const auto connect_result_future = connect_result.get_future().share();
    client->connect("localhost", port_string, "/",
        [&connect_result](const std::error_code &ec) { connect_result.set_value(ec); });

    ASSERT_TRUE(wait_ready(connect_result_future));
    EXPECT_TRUE(connect_result_future.get());
    EXPECT_FALSE(client->is_connected());
    executor.stop();
}

TEST(SecureWebSocketClientTest, connect_reports_websocket_handshake_failure)
{
    const auto port = start_secure_websocket_path_server("/ws");
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::error_code> connect_result;
    const auto connect_result_future = connect_result.get_future().share();
    client->connect("localhost", port_string, "/wrong",
        [&connect_result](const std::error_code &ec) { connect_result.set_value(ec); });

    ASSERT_TRUE(wait_ready(connect_result_future));
    EXPECT_TRUE(connect_result_future.get());
    EXPECT_FALSE(client->is_connected());
    executor.stop();
}

TEST(SecureWebSocketClientTest, write_before_connect_reports_not_connected)
{
    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);

    std::promise<std::error_code> write_result;
    const auto write_result_future = write_result.get_future().share();
    client->write("ping",
        [&write_result](const std::error_code &ec) { write_result.set_value(ec); });

    ASSERT_TRUE(wait_ready(write_result_future));
    EXPECT_EQ(write_result_future.get(), std::make_error_code(std::errc::not_connected));
    executor.stop();
}

TEST(SecureWebSocketClientTest, double_connect_reports_already_connected)
{
    const auto port = start_secure_echo_server();
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<void> connected;
    const auto connected_future = connected.get_future().share();
    client->connect("localhost", port_string, "/", [&connected](const std::error_code &ec) {
        if (!ec) {
            connected.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(connected_future));

    std::promise<std::error_code> second_connect_result;
    const auto second_connect_result_future = second_connect_result.get_future().share();
    client->connect("localhost", port_string, "/",
        [&second_connect_result](
            const std::error_code &ec) { second_connect_result.set_value(ec); });

    ASSERT_TRUE(wait_ready(second_connect_result_future));
    EXPECT_EQ(second_connect_result_future.get(),
        std::make_error_code(std::errc::already_connected));

    std::promise<void> closed;
    const auto closed_future = closed.get_future().share();
    client->close([&closed](const std::error_code &ec) {
        if (!ec) {
            closed.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(closed_future));
    executor.stop();
}

TEST(SecureWebSocketClientTest, ping_interval_sends_ping_and_receives_pong)
{
    constexpr auto ping_message = "custom-ping";
    const auto port = start_secure_ping_pong_server(ping_message);
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_ping_message_generator([]() { return std::string(ping_message); });
    client->set_ping_interval(std::chrono::seconds { 1 });

    std::promise<void> connected;
    const auto connected_future = connected.get_future().share();
    client->connect("localhost", port_string, "/", [&connected](const std::error_code &ec) {
        if (!ec) {
            connected.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(connected_future));
    EXPECT_TRUE(client->is_connected());

    std::promise<std::string> pong_received;
    const auto pong_received_future = pong_received.get_future().share();
    client->set_message_handler([&pong_received](const char *data, size_t size) {
        pong_received.set_value(std::string(data, size));
    });

    ASSERT_TRUE(wait_ready(pong_received_future));
    EXPECT_EQ(pong_received_future.get(), "pong");

    std::promise<void> closed;
    const auto closed_future = closed.get_future().share();
    client->close([&closed](const std::error_code &ec) {
        if (!ec) {
            closed.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(closed_future));
    EXPECT_FALSE(client->is_connected());
    executor.stop();
}

TEST(SecureWebSocketClientTest, read_timeout_disconnects_when_idle)
{
    const auto port = start_secure_echo_server();
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_read_timeout(std::chrono::seconds { 1 });

    std::promise<void> connected;
    const auto connected_future = connected.get_future().share();
    client->connect("localhost", port_string, "/", [&connected](const std::error_code &ec) {
        if (!ec) {
            connected.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(connected_future));
    EXPECT_TRUE(client->is_connected());

    std::promise<std::error_code> disconnected;
    const auto disconnected_future = disconnected.get_future().share();
    client->set_disconnect_handler(
        [&disconnected](const std::error_code &ec) { disconnected.set_value(ec); });

    ASSERT_TRUE(wait_ready(disconnected_future));
    EXPECT_EQ(disconnected_future.get(), std::make_error_code(std::errc::timed_out));
    EXPECT_FALSE(client->is_connected());
    executor.stop();
}
