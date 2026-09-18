/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "benchmark_support.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <future>
#include <string>
#include <system_error>
#include <thread>

namespace secure_websocket_client_benchmark {

namespace {

boost::asio::ssl::context make_server_ssl_context()
{
    namespace ssl = boost::asio::ssl;
    ssl::context ssl_context(ssl::context::tlsv12_server);
    ssl_context.use_certificate_chain_file(BENCHMARK_CERT_DIR "/test-cert.pem");
    ssl_context.use_private_key_file(BENCHMARK_CERT_DIR "/test-key.pem",
        ssl::context::file_format::pem);
    return ssl_context;
}

void run_secure_echo_session(boost::asio::ip::tcp::socket socket)
{
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    namespace ssl = boost::asio::ssl;
    using websocket_stream = websocket::stream<ssl::stream<beast::tcp_stream>>;

    auto ssl_context = make_server_ssl_context();

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

void run_secure_push_session(boost::asio::ip::tcp::socket socket)
{
    namespace beast = boost::beast;
    namespace net = boost::asio;
    namespace websocket = beast::websocket;
    using websocket_stream = websocket::stream<boost::asio::ssl::stream<beast::tcp_stream>>;

    auto ssl_context = make_server_ssl_context();

    boost::system::error_code ec;
    websocket_stream ws(
        boost::asio::ssl::stream<beast::tcp_stream>(std::move(socket), ssl_context));
    ws.next_layer().handshake(boost::asio::ssl::stream_base::server, ec);
    if (ec) {
        return;
    }

    ws.accept(ec);
    if (ec) {
        return;
    }

    const std::string payload(benchmark_payload);
    for (;;) {
        ws.write(net::buffer(payload), ec);
        if (ec) {
            break;
        }
    }
}

uint16_t start_secure_server(void (*session)(boost::asio::ip::tcp::socket))
{
    namespace net = boost::asio;
    using tcp = net::ip::tcp;

    auto ioc = std::make_shared<net::io_context>();
    auto acceptor = std::make_shared<tcp::acceptor>(*ioc, tcp::endpoint(tcp::v4(), 0));
    const auto port = acceptor->local_endpoint().port();

    std::thread([ioc, acceptor, session]() {
        tcp::socket socket(*ioc);
        boost::system::error_code ec;
        acceptor->accept(socket, ec);
        if (ec) {
            return;
        }
        session(std::move(socket));
    }).detach();

    return port;
}

} // namespace

uint16_t start_secure_echo_server()
{
    return start_secure_server(run_secure_echo_session);
}

uint16_t start_secure_push_server()
{
    return start_secure_server(run_secure_push_session);
}

bool connect_client(cpp_components::secure_websocket_client::SecureWebSocketClient &client,
    const std::string &port_string)
{
    std::promise<void> connected;
    const auto connected_future = connected.get_future().share();
    client.connect("localhost", port_string, "/", [&connected](const std::error_code &ec) {
        if (!ec) {
            connected.set_value();
        }
    });

    return wait_ready(connected_future) && client.is_connected();
}

void close_client(cpp_components::secure_websocket_client::SecureWebSocketClient &client)
{
    if (!client.is_connected()) {
        return;
    }

    std::promise<void> closed;
    const auto closed_future = closed.get_future().share();
    client.close([&closed](const std::error_code &ec) {
        if (!ec) {
            closed.set_value();
        }
    });
    wait_ready(closed_future);
}

} // namespace secure_websocket_client_benchmark
