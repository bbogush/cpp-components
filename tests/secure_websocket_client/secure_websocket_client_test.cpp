/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "cpp_components/executor/executor.h"
#include "cpp_components/secure_websocket_client/reconnecting_secure_websocket_client.h"
#include "cpp_components/secure_websocket_client/secure_websocket_client.h"

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
#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <thread>
#include <utility>
#include <vector>

namespace {

constexpr auto wait_timeout = std::chrono::seconds { 5 };

template<typename T>
bool wait_ready(const std::shared_future<T> &future)
{
    return future.wait_for(wait_timeout) == std::future_status::ready;
}

constexpr std::string_view echo_repeat_prefix = "repeat:";

void run_secure_echo_session(boost::asio::ip::tcp::socket socket, bool close_after_first_message,
    const std::function<void(std::string)> &on_target = nullptr)
{
    namespace beast = boost::beast;
    namespace http = beast::http;
    namespace websocket = beast::websocket;
    namespace ssl = boost::asio::ssl;
    using ssl_stream = ssl::stream<beast::tcp_stream>;
    using websocket_stream = websocket::stream<ssl_stream>;

    ssl::context ssl_context(ssl::context::tlsv12_server);
    ssl_context.use_certificate_chain_file(TEST_CERT_DIR "/test-cert.pem");
    ssl_context.use_private_key_file(TEST_CERT_DIR "/test-key.pem", ssl::context::file_format::pem);

    boost::system::error_code ec;
    ssl_stream stream(beast::tcp_stream(std::move(socket)), ssl_context);
    stream.handshake(ssl::stream_base::server, ec);
    if (ec) {
        return;
    }

    beast::flat_buffer request_buffer;
    http::request<http::string_body> request;
    http::read(stream, request_buffer, request, ec);
    if (ec) {
        return;
    }

    if (on_target) {
        on_target(std::string(request.target()));
    }

    websocket_stream ws(std::move(stream));
    ws.accept(request, ec);
    if (ec) {
        return;
    }

    for (;;) {
        beast::flat_buffer buffer;
        ws.read(buffer, ec);
        if (ec) {
            break;
        }

        // "repeat:N" asks for a reply of N bytes, to test messages larger than the client
        // can write.
        const auto text = beast::buffers_to_string(buffer.data());
        if (text.starts_with(echo_repeat_prefix)) {
            const std::string reply(std::stoul(text.substr(echo_repeat_prefix.size())), 'x');
            ws.write(boost::asio::buffer(reply), ec);
        } else {
            ws.write(buffer.data(), ec);
        }
        if (ec) {
            break;
        }
        buffer.consume(buffer.size());

        if (close_after_first_message) {
            break;
        }
    }
}

uint16_t start_secure_echo_server(bool close_after_first_message = false, int accept_count = 1,
    std::function<void(std::string)> on_target = nullptr)
{
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    auto ioc = std::make_shared<net::io_context>();
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor->local_endpoint().port();

    std::thread([ioc, acceptor, close_after_first_message, accept_count,
                    on_target = std::move(on_target)]() {
        for (int i = 0; i < accept_count; ++i) {
            tcp::socket socket(*ioc);
            boost::system::error_code ec;
            acceptor->accept(socket, ec);
            if (ec) {
                return;
            }
            run_secure_echo_session(std::move(socket), close_after_first_message, on_target);
        }
    }).detach();

    return port;
}

void start_secure_echo_server_on_port(uint16_t port)
{
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    auto ioc = std::make_shared<net::io_context>();
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc, tcp::endpoint(tcp::v4(), port));

    std::thread([ioc, acceptor]() {
        tcp::socket socket(*ioc);
        boost::system::error_code ec;
        acceptor->accept(socket, ec);
        if (ec) {
            return;
        }
        run_secure_echo_session(std::move(socket), false);
    }).detach();
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
            const auto message = std::string(static_cast<const char *>(data.data()), data.size());
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

uint16_t start_secure_protocol_ping_server(std::shared_ptr<std::promise<void>> ping_received)
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

    std::thread([ioc, acceptor, ping_received = std::move(ping_received)]() {
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

        auto signaled = std::make_shared<std::atomic<bool>>(false);
        ws.control_callback(
            [ping_received, signaled](websocket::frame_type kind, beast::string_view) {
                if (kind == websocket::frame_type::ping &&
                    !signaled->exchange(true, std::memory_order_acq_rel)) {
                    ping_received->set_value();
                }
            });

        for (;;) {
            beast::flat_buffer buffer;
            ws.read(buffer, ec);
            if (ec) {
                break;
            }
            buffer.consume(buffer.size());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor->start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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
    ASSERT_FALSE(executor.start());
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

TEST(SecureWebSocketClientTest, reconnect_after_close_reuses_client)
{
    const auto port = start_secure_echo_server(false, 2);
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    for (int attempt = 0; attempt < 2; ++attempt) {
        std::promise<void> connected;
        const auto connected_future = connected.get_future().share();
        client->connect("localhost", port_string, "/", [&connected](const std::error_code &ec) {
            if (!ec) {
                connected.set_value();
            }
        });

        ASSERT_TRUE(wait_ready(connected_future)) << "attempt " << attempt;
        EXPECT_TRUE(client->is_connected());

        std::promise<std::string> message_received;
        const auto message_received_future = message_received.get_future().share();
        client->set_message_handler([&message_received](const char *data, size_t size) {
            message_received.set_value(std::string(data, size));
        });

        const auto payload = "ping-" + std::to_string(attempt);
        std::promise<void> message_written;
        const auto message_written_future = message_written.get_future().share();
        client->write(payload, [&message_written](const std::error_code &ec) {
            if (!ec) {
                message_written.set_value();
            }
        });

        ASSERT_TRUE(wait_ready(message_written_future)) << "attempt " << attempt;
        ASSERT_TRUE(wait_ready(message_received_future)) << "attempt " << attempt;
        EXPECT_EQ(message_received_future.get(), payload);

        std::promise<void> closed;
        const auto closed_future = closed.get_future().share();
        client->close([&closed](const std::error_code &ec) {
            if (!ec) {
                closed.set_value();
            }
        });

        ASSERT_TRUE(wait_ready(closed_future)) << "attempt " << attempt;
        EXPECT_FALSE(client->is_connected());
    }

    executor.stop();
}

TEST(SecureWebSocketClientTest, ping_interval_sends_ping_and_receives_pong)
{
    constexpr auto ping_message = "custom-ping";
    const auto port = start_secure_ping_pong_server(ping_message);
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
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

TEST(SecureWebSocketClientTest, ping_interval_sends_protocol_ping_without_generator)
{
    auto ping_received = std::make_shared<std::promise<void>>();
    const auto ping_received_future = ping_received->get_future().share();
    const auto port = start_secure_protocol_ping_server(ping_received);
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
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
    ASSERT_TRUE(wait_ready(ping_received_future));

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
    ASSERT_FALSE(executor.start());
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

TEST(SecureWebSocketClientTest, read_timeout_does_not_disconnect_while_messages_arrive)
{
    const auto port = start_secure_echo_server();
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
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

    std::atomic<int> received { 0 };
    client->set_message_handler([&received](const char *, size_t) { received.fetch_add(1); });

    std::promise<std::error_code> disconnected;
    const auto disconnected_future = disconnected.get_future().share();
    std::atomic<bool> is_writing { true };
    client->set_disconnect_handler([&disconnected, &is_writing](const std::error_code &ec) {
        // Only a disconnect after the writes stop is expected.
        EXPECT_FALSE(is_writing.load());
        disconnected.set_value(ec);
    });

    // Echoes arrive every 300 ms for 2.5 timeout periods: the connection must stay open.
    const auto writes_end = std::chrono::steady_clock::now() + std::chrono::milliseconds { 2500 };
    while (std::chrono::steady_clock::now() < writes_end) {
        client->write("keepalive", nullptr);
        std::this_thread::sleep_for(std::chrono::milliseconds { 300 });
    }
    is_writing.store(false);
    EXPECT_TRUE(client->is_connected());
    EXPECT_GT(received.load(), 0);

    // Once messages stop, the timeout still fires.
    ASSERT_EQ(disconnected_future.wait_for(std::chrono::seconds { 3 }), std::future_status::ready);
    EXPECT_EQ(disconnected_future.get(), std::make_error_code(std::errc::timed_out));
    executor.stop();
}

namespace {

using cpp_components::secure_websocket_client::SecureWebSocketClient;

std::shared_ptr<SecureWebSocketClient> connect_to_echo_server(
    cpp_components::executor::Executor &executor)
{
    const auto port_string = std::to_string(start_secure_echo_server());
    auto client = SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::error_code> connected;
    const auto connected_future = connected.get_future().share();
    client->connect("localhost", port_string, "/",
        [&connected](const std::error_code &ec) { connected.set_value(ec); });
    if (!wait_ready(connected_future) || connected_future.get()) {
        return nullptr;
    }
    return client;
}

// Runs callback on the executor thread and returns its result.
template<typename F>
auto run_on_executor(cpp_components::executor::Executor &executor, F callback)
{
    std::promise<decltype(callback())> result;
    auto result_future = result.get_future();
    executor.post([&result, &callback]() { result.set_value(callback()); });
    return result_future.get();
}

// Collects echoed messages; wait_for() returns them once count have arrived.
class MessageCollector {
public:
    void add(const char *data, size_t size)
    {
        const std::lock_guard lock(mutex);
        messages.emplace_back(data, size);
        changed.notify_all();
    }

    std::vector<std::string> wait_for(size_t count)
    {
        std::unique_lock lock(mutex);
        changed.wait_for(lock, wait_timeout, [this, count]() { return messages.size() >= count; });
        return messages;
    }

private:
    std::mutex mutex;
    std::condition_variable changed;
    std::vector<std::string> messages;
};

std::span<const char> as_span(std::string_view text)
{
    return { text.data(), text.size() };
}

} // namespace

TEST(SecureWebSocketClientTest, write_now_sends_message_from_executor_thread)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = connect_to_echo_server(executor);
    ASSERT_TRUE(client);

    MessageCollector collector;
    client->set_message_handler(
        [&collector](const char *data, size_t size) { collector.add(data, size); });

    const auto ec = run_on_executor(executor,
        [&client]() { return client->write_now(as_span("hello")); });
    EXPECT_FALSE(ec);
    EXPECT_EQ(collector.wait_for(1), std::vector<std::string> { "hello" });

    client->close();
    executor.stop();
}

TEST(SecureWebSocketClientTest, write_now_rejects_call_from_another_thread)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = connect_to_echo_server(executor);
    ASSERT_TRUE(client);

    EXPECT_EQ(client->write_now(as_span("hello")),
        std::make_error_code(std::errc::operation_not_permitted));

    client->close();
    executor.stop();
}

TEST(SecureWebSocketClientTest, write_now_rejects_when_not_connected)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = SecureWebSocketClient::create(executor);

    const auto ec = run_on_executor(executor,
        [&client]() { return client->write_now(as_span("hello")); });
    EXPECT_EQ(ec, std::make_error_code(std::errc::not_connected));
    executor.stop();
}

TEST(SecureWebSocketClientTest, write_now_rejects_message_larger_than_slot)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = connect_to_echo_server(executor);
    ASSERT_TRUE(client);

    MessageCollector collector;
    client->set_message_handler(
        [&collector](const char *data, size_t size) { collector.add(data, size); });

    const std::string largest(SecureWebSocketClient::get_write_slot_size(), 'x');
    const std::string too_large(SecureWebSocketClient::get_write_slot_size() + 1, 'x');
    const auto ec_too_large = run_on_executor(executor,
        [&client, &too_large]() { return client->write_now(as_span(too_large)); });
    const auto ec_largest = run_on_executor(executor,
        [&client, &largest]() { return client->write_now(as_span(largest)); });

    EXPECT_EQ(ec_too_large, std::make_error_code(std::errc::message_size));
    EXPECT_FALSE(ec_largest);
    EXPECT_EQ(collector.wait_for(1), std::vector<std::string> { largest });

    client->close();
    executor.stop();
}

TEST(SecureWebSocketClientTest, write_now_rejects_when_all_slots_are_in_use)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = connect_to_echo_server(executor);
    ASSERT_TRUE(client);

    MessageCollector collector;
    client->set_message_handler(
        [&collector](const char *data, size_t size) { collector.add(data, size); });

    const auto slot_count = SecureWebSocketClient::get_write_slot_count();
    // In one executor callback no write can complete, so every message holds its slot.
    const auto results = run_on_executor(executor, [&client, slot_count]() {
        std::vector<std::error_code> codes;
        for (size_t i = 0; i <= slot_count; ++i) {
            codes.push_back(client->write_now(as_span(std::to_string(i))));
        }
        return codes;
    });

    ASSERT_EQ(results.size(), slot_count + 1);
    for (size_t i = 0; i < slot_count; ++i) {
        EXPECT_FALSE(results[i]) << "message " << i;
    }
    EXPECT_EQ(results.back(), std::make_error_code(std::errc::no_buffer_space));

    // The queued messages are all sent, in order.
    const auto messages = collector.wait_for(slot_count);
    ASSERT_EQ(messages.size(), slot_count);
    for (size_t i = 0; i < slot_count; ++i) {
        EXPECT_EQ(messages[i], std::to_string(i));
    }

    // Slots are reused once their writes complete.
    const auto ec = run_on_executor(executor,
        [&client]() { return client->write_now(as_span("again")); });
    EXPECT_FALSE(ec);
    EXPECT_EQ(collector.wait_for(slot_count + 1).back(), "again");

    client->close();
    executor.stop();
}

TEST(SecureWebSocketClientTest, write_reports_message_larger_than_slot_to_handler)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = connect_to_echo_server(executor);
    ASSERT_TRUE(client);

    std::promise<std::error_code> written;
    const auto written_future = written.get_future().share();
    client->write(std::string(SecureWebSocketClient::get_write_slot_size() + 1, 'x'),
        [&written](const std::error_code &ec) { written.set_value(ec); });

    ASSERT_TRUE(wait_ready(written_future));
    EXPECT_EQ(written_future.get(), std::make_error_code(std::errc::message_size));
    EXPECT_TRUE(client->is_connected());

    client->close();
    executor.stop();
}

TEST(SecureWebSocketClientTest, write_now_and_write_are_sent_in_queue_order)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = connect_to_echo_server(executor);
    ASSERT_TRUE(client);

    MessageCollector collector;
    client->set_message_handler(
        [&collector](const char *data, size_t size) { collector.add(data, size); });

    // write() posts, so "2" is queued after "3", which write_now() queues right away.
    const auto ec = run_on_executor(executor, [&client]() {
        auto first = client->write_now(as_span("1"));
        client->write("2", nullptr);
        auto third = client->write_now(as_span("3"));
        return first ? first : third;
    });
    EXPECT_FALSE(ec);
    EXPECT_EQ(collector.wait_for(3), (std::vector<std::string> { "1", "3", "2" }));

    client->close();
    executor.stop();
}

TEST(ReconnectingSecureWebSocketClientTest, reconnects_after_unexpected_disconnect)
{
    const auto port = start_secure_echo_server(true, 2);
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client =
        cpp_components::secure_websocket_client::ReconnectingSecureWebSocketClient::create(
            executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_initial_reconnect_delay(std::chrono::milliseconds { 100 });
    client->set_max_reconnect_delay(std::chrono::seconds { 1 });

    std::promise<void> first_connected;
    std::promise<void> second_connected;
    auto first_connected_future = first_connected.get_future().share();
    auto second_connected_future = second_connected.get_future().share();
    std::atomic<int> connect_count { 0 };
    client->connect([port_string](auto ready) { ready("localhost", port_string, "/"); },
        [&connect_count, &first_connected, &second_connected](const std::error_code &ec) {
            if (ec) {
                return;
            }

            const auto count = connect_count.fetch_add(1) + 1;
            if (count == 1) {
                first_connected.set_value();
            } else if (count == 2) {
                second_connected.set_value();
            }
        });

    ASSERT_TRUE(wait_ready(first_connected_future));
    EXPECT_TRUE(client->is_connected());

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
    ASSERT_TRUE(wait_ready(second_connected_future));
    EXPECT_TRUE(client->is_connected());

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

TEST(SecureWebSocketClientTest, connect_after_connection_refused_reuses_client)
{
    const auto refused_port = closed_port();
    const auto good_port = start_secure_echo_server();
    const auto refused_port_string = std::to_string(refused_port);
    const auto good_port_string = std::to_string(good_port);

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<std::error_code> first_result;
    const auto first_future = first_result.get_future().share();
    client->connect("localhost", refused_port_string, "/",
        [&first_result](const std::error_code &ec) { first_result.set_value(ec); });

    ASSERT_TRUE(wait_ready(first_future));
    EXPECT_TRUE(first_future.get());
    EXPECT_FALSE(client->is_connected());

    std::promise<void> connected;
    const auto connected_future = connected.get_future().share();
    client->connect("localhost", good_port_string, "/", [&connected](const std::error_code &ec) {
        if (!ec) {
            connected.set_value();
        } else {
            ADD_FAILURE() << "second connect failed: " << ec.message();
        }
    });

    ASSERT_TRUE(wait_ready(connected_future));
    EXPECT_TRUE(client->is_connected());

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

TEST(ReconnectingSecureWebSocketClientTest, reconnects_after_initial_connect_failure)
{
    const auto port = closed_port();
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client =
        cpp_components::secure_websocket_client::ReconnectingSecureWebSocketClient::create(
            executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_initial_reconnect_delay(std::chrono::milliseconds { 100 });
    client->set_max_reconnect_delay(std::chrono::seconds { 1 });

    std::promise<void> connected;
    const auto connected_future = connected.get_future().share();
    client->connect([port_string](auto ready) { ready("localhost", port_string, "/"); },
        [&connected](const std::error_code &ec) {
            if (!ec) {
                connected.set_value();
            }
        });

    std::this_thread::sleep_for(std::chrono::milliseconds { 150 });
    start_secure_echo_server_on_port(port);

    ASSERT_TRUE(wait_ready(connected_future));
    EXPECT_TRUE(client->is_connected());

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

TEST(ReconnectingSecureWebSocketClientTest, close_stops_reconnect_attempts)
{
    const auto port_string = std::to_string(closed_port());

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client =
        cpp_components::secure_websocket_client::ReconnectingSecureWebSocketClient::create(
            executor);
    client->set_initial_reconnect_delay(std::chrono::seconds { 60 });
    client->set_max_reconnect_delay(std::chrono::seconds { 60 });

    std::promise<std::error_code> first_connect_result;
    const auto first_connect_result_future = first_connect_result.get_future().share();
    std::atomic<int> connect_handler_calls { 0 };
    client->connect([port_string](auto ready) { ready("127.0.0.1", port_string, "/"); },
        [&connect_handler_calls, &first_connect_result](const std::error_code &ec) {
            if (connect_handler_calls.fetch_add(1) == 0) {
                first_connect_result.set_value(ec);
            }
        });

    ASSERT_TRUE(wait_ready(first_connect_result_future));
    EXPECT_TRUE(first_connect_result_future.get());
    EXPECT_FALSE(client->is_connected());

    std::promise<void> closed;
    const auto closed_future = closed.get_future().share();
    client->close([&closed](const std::error_code &ec) {
        if (!ec) {
            closed.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(closed_future));
    EXPECT_FALSE(client->is_connected());

    const auto calls_at_close = connect_handler_calls.load();
    std::this_thread::sleep_for(std::chrono::milliseconds { 100 });
    EXPECT_EQ(connect_handler_calls.load(), calls_at_close);
    executor.stop();
}

TEST(ReconnectingSecureWebSocketClientTest, prepares_new_resource_after_unexpected_disconnect)
{
    std::mutex targets_mutex;
    std::vector<std::string> targets;
    std::promise<void> second_target_recorded;
    auto second_target_future = second_target_recorded.get_future().share();

    const auto port = start_secure_echo_server(true, 2,
        [&targets_mutex, &targets, &second_target_recorded](std::string target) {
            std::lock_guard lock(targets_mutex);
            targets.push_back(std::move(target));
            if (targets.size() == 2) {
                second_target_recorded.set_value();
            }
        });
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client =
        cpp_components::secure_websocket_client::ReconnectingSecureWebSocketClient::create(
            executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");
    client->set_initial_reconnect_delay(std::chrono::milliseconds { 100 });
    client->set_max_reconnect_delay(std::chrono::seconds { 1 });

    std::atomic<int> prepare_count { 0 };
    std::promise<void> first_connected;
    std::promise<void> second_connected;
    auto first_connected_future = first_connected.get_future().share();
    auto second_connected_future = second_connected.get_future().share();
    std::atomic<int> connect_count { 0 };

    client->connect(
        [&prepare_count, port_string](auto ready) {
            const auto count = prepare_count.fetch_add(1) + 1;
            ready("localhost", port_string, "/key-" + std::to_string(count));
        },
        [&connect_count, &first_connected, &second_connected](const std::error_code &ec) {
            if (ec) {
                return;
            }

            const auto count = connect_count.fetch_add(1) + 1;
            if (count == 1) {
                first_connected.set_value();
            } else if (count == 2) {
                second_connected.set_value();
            }
        });

    ASSERT_TRUE(wait_ready(first_connected_future));

    std::promise<void> message_written;
    const auto message_written_future = message_written.get_future().share();
    client->write("ping", [&message_written](const std::error_code &ec) {
        if (!ec) {
            message_written.set_value();
        }
    });

    ASSERT_TRUE(wait_ready(message_written_future));
    ASSERT_TRUE(wait_ready(second_connected_future));
    ASSERT_TRUE(wait_ready(second_target_future));

    {
        std::lock_guard lock(targets_mutex);
        ASSERT_EQ(targets.size(), 2u);
        EXPECT_EQ(targets[0], "/key-1");
        EXPECT_EQ(targets[1], "/key-2");
    }
    EXPECT_GE(prepare_count.load(), 2);

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

TEST(ReconnectingSecureWebSocketClientTest, ignores_late_connection_ready_after_close)
{
    const auto port = start_secure_echo_server();
    const auto port_string = std::to_string(port);

    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client =
        cpp_components::secure_websocket_client::ReconnectingSecureWebSocketClient::create(
            executor);
    client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem");

    std::promise<cpp_components::secure_websocket_client::ReconnectingSecureWebSocketClient::
            ConnectionReadyHandler>
        ready_handler;
    const auto ready_handler_future = ready_handler.get_future().share();

    std::atomic<bool> connect_handler_called { false };
    client->connect([&ready_handler](auto ready) { ready_handler.set_value(std::move(ready)); },
        [&connect_handler_called](const std::error_code &) { connect_handler_called.store(true); });

    ASSERT_TRUE(wait_ready(ready_handler_future));
    auto ready = ready_handler_future.get();

    std::promise<void> closed;
    const auto closed_future = closed.get_future().share();
    client->close([&closed](const std::error_code &ec) {
        if (!ec) {
            closed.set_value();
        }
    });
    ASSERT_TRUE(wait_ready(closed_future));

    ready("localhost", port_string, "/");
    std::this_thread::sleep_for(std::chrono::milliseconds { 200 });
    EXPECT_FALSE(connect_handler_called.load());
    EXPECT_FALSE(client->is_connected());
    executor.stop();
}

TEST(SecureWebSocketClientTest, set_ca_certificate_reports_missing_file)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = cpp_components::secure_websocket_client::SecureWebSocketClient::create(executor);

    EXPECT_TRUE(client->set_ca_certificate("/nonexistent/ca-cert.pem"));
    EXPECT_FALSE(client->set_ca_certificate(""));
    EXPECT_FALSE(client->set_ca_certificate(TEST_CERT_DIR "/test-cert.pem"));
    executor.stop();
}

TEST(SecureWebSocketClientTest, largest_message_is_followed_by_read_padding)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = connect_to_echo_server(executor);
    ASSERT_TRUE(client);

    std::promise<std::string> received;
    auto received_future = received.get_future();
    client->set_message_handler([&received](const char *data, size_t size) {
        // Touch the last padding byte, as a parser reading past the end would. The read buffer
        // is a member of the client, so a missing padding would read the next member rather
        // than fault; the guarantee itself comes from the buffer layout.
        volatile char last_padding_byte =
            data[size + SecureWebSocketClient::get_read_padding() - 1];
        static_cast<void>(last_padding_byte);
        received.set_value(std::string(data, size));
    });

    const std::string largest(SecureWebSocketClient::get_read_buffer_size(), 'x');
    client->write("repeat:" + std::to_string(largest.size()), nullptr);

    ASSERT_EQ(received_future.wait_for(wait_timeout), std::future_status::ready);
    EXPECT_EQ(received_future.get(), largest);

    client->close();
    executor.stop();
}

TEST(SecureWebSocketClientTest, message_larger_than_read_buffer_disconnects)
{
    cpp_components::executor::Executor executor {};
    ASSERT_FALSE(executor.start());
    auto client = connect_to_echo_server(executor);
    ASSERT_TRUE(client);

    std::atomic<bool> received { false };
    client->set_message_handler([&received](const char *, size_t) { received = true; });
    std::promise<std::error_code> disconnected;
    const auto disconnected_future = disconnected.get_future().share();
    client->set_disconnect_handler(
        [&disconnected](const std::error_code &ec) { disconnected.set_value(ec); });

    // The echo is one byte larger than the largest message the client accepts.
    client->write("repeat:" + std::to_string(SecureWebSocketClient::get_read_buffer_size() + 1),
        nullptr);

    // Beast rejects the message and starts closing; the error reported depends on how the
    // close ends (e.g. a TLS error while the server is still sending), so only check that the
    // connection failed without delivering the message.
    ASSERT_TRUE(wait_ready(disconnected_future));
    EXPECT_TRUE(disconnected_future.get());
    EXPECT_FALSE(received.load());
    executor.stop();
}
