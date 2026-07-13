/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "secure_websocket_client.h"

#include <boost/asio/connect.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <chrono>
#include <utility>

namespace cpp_components::secure_websocket_client {

namespace {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;

constexpr auto connect_timeout = std::chrono::seconds(30);

} // namespace

std::shared_ptr<SecureWebSocketClient> SecureWebSocketClient::create(executor::Executor &executor)
{
    return std::shared_ptr<SecureWebSocketClient>(new SecureWebSocketClient(executor));
}

SecureWebSocketClient::SecureWebSocketClient(executor::Executor &executor) :
    executor(executor), ssl_context(ssl::context::tlsv12_client), resolver(executor.get_context()),
    ws(executor.get_context(), ssl_context), ping_timer(executor)
{
    ssl_context.set_default_verify_paths();
    ssl_context.set_verify_mode(ssl::verify_peer);
}

SecureWebSocketClient::~SecureWebSocketClient()
{
    const auto current_state = state.load(std::memory_order_acquire);
    if (current_state != ConnectionState::connecting &&
        current_state != ConnectionState::connected) {
        return;
    }

    set_state(ConnectionState::closing);
    cancel_pending_operations();
    fail_pending_writes();
    close_socket();
    set_state(ConnectionState::disconnected);
}

void SecureWebSocketClient::set_ca_certificate(const std::string &ca_certificate_file)
{
    if (ca_certificate_file.empty()) {
        return;
    }

    ssl_context.load_verify_file(ca_certificate_file);
}

void SecureWebSocketClient::connect(std::string host, std::string port, std::string resource,
    ConnectHandler handler)
{
    this->host = std::move(host);
    this->port = std::move(port);
    this->resource = std::move(resource);

    auto self = shared_from_this();
    auto connect_handler = [self, handler = std::move(handler)]() mutable {
        self->do_connect(std::move(handler));
    };
    executor.post(std::move(connect_handler));
}

void SecureWebSocketClient::write(std::string message, WriteHandler handler)
{
    auto self = shared_from_this();
    auto write_handler = [self, message = std::move(message),
                             handler = std::move(handler)]() mutable {
        self->do_write(std::move(message), std::move(handler));
    };
    executor.post(std::move(write_handler));
}

void SecureWebSocketClient::close(CloseHandler handler)
{
    auto self = shared_from_this();
    auto close_handler = [self, handler = std::move(handler)]() mutable {
        self->do_close(handler);
    };
    executor.post(std::move(close_handler));
}

void SecureWebSocketClient::set_message_handler(MessageHandler handler)
{
    auto self = shared_from_this();
    auto message_handler = [self, handler = std::move(handler)]() mutable {
        self->message_handler = std::move(handler);
    };
    executor.post(std::move(message_handler));
}

void SecureWebSocketClient::set_disconnect_handler(DisconnectHandler handler)
{
    auto self = shared_from_this();
    auto disconnect_handler = [self, handler = std::move(handler)]() mutable {
        self->disconnect_handler = std::move(handler);
    };
    executor.post(std::move(disconnect_handler));
}

void SecureWebSocketClient::set_ping_message_generator(PingMessageGenerator generator)
{
    auto self = shared_from_this();
    auto set_generator = [self, generator = std::move(generator)]() mutable {
        self->ping_message_generator = std::move(generator);
    };
    executor.post(std::move(set_generator));
}

void SecureWebSocketClient::set_ping_interval(std::chrono::seconds interval)
{
    auto self = shared_from_this();
    auto interval_handler = [self, interval]() {
        self->ping_interval = interval;
        if (!self->is_connected()) {
            return;
        }

        self->stop_ping_timer();
        self->start_ping_timer();
    };
    executor.post(std::move(interval_handler));
}

bool SecureWebSocketClient::is_connected() const
{
    return state.load(std::memory_order_acquire) == ConnectionState::connected;
}

void SecureWebSocketClient::do_connect(ConnectHandler handler)
{
    if (state.load(std::memory_order_acquire) != ConnectionState::disconnected) {
        if (handler) {
            handler(std::make_error_code(std::errc::already_connected));
        }
        return;
    }

    set_state(ConnectionState::connecting);

    auto self = shared_from_this();
    auto resolve_handler = [self, handler = std::move(handler)](const boost::system::error_code &ec,
                               const Tcp::resolver::results_type &results) mutable {
        self->handle_resolve(std::move(handler), ec, results);
    };
    resolver.async_resolve(host, port, std::move(resolve_handler));
}

void SecureWebSocketClient::handle_resolve(ConnectHandler handler,
    const boost::system::error_code &ec, const Tcp::resolver::results_type &results)
{
    if (!is_connecting()) {
        return;
    }

    if (ec) {
        fail_connection(ec, handler);
        return;
    }

    auto self = shared_from_this();
    beast::get_lowest_layer(ws).expires_after(connect_timeout);
    auto connect_handler =
        [self, handler = std::move(handler)](const boost::system::error_code &connect_ec,
            const Tcp::endpoint &endpoint) mutable {
            self->handle_connect(std::move(handler), connect_ec, endpoint);
        };
    beast::get_lowest_layer(ws).async_connect(results, std::move(connect_handler));
}

void SecureWebSocketClient::handle_connect(ConnectHandler handler,
    const boost::system::error_code &ec, const Tcp::endpoint &)
{
    if (!is_connecting()) {
        return;
    }

    if (ec) {
        fail_connection(ec, handler);
        return;
    }

    beast::get_lowest_layer(ws).expires_after(connect_timeout);

    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
        fail_connection(boost::system::error_code(static_cast<int>(::ERR_get_error()),
                            boost::asio::error::get_ssl_category()),
            handler);
        return;
    }

    ws.next_layer().set_verify_callback(ssl::host_name_verification(host));

    auto self = shared_from_this();
    auto ssl_handshake_handler = [self, handler = std::move(handler)](
                                     const boost::system::error_code &ssl_ec) mutable {
        self->handle_ssl_handshake(std::move(handler), ssl_ec);
    };
    ws.next_layer().async_handshake(ssl::stream_base::client, std::move(ssl_handshake_handler));
}

void SecureWebSocketClient::handle_ssl_handshake(ConnectHandler handler,
    const boost::system::error_code &ec)
{
    if (!is_connecting()) {
        return;
    }

    if (ec) {
        fail_connection(ec, handler);
        return;
    }

    beast::get_lowest_layer(ws).expires_never();
    ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

    auto self = shared_from_this();
    auto websocket_handshake_handler = [self, handler = std::move(handler)](
                                           const boost::system::error_code &handshake_ec) mutable {
        self->handle_websocket_handshake(handler, handshake_ec);
    };
    ws.async_handshake(host, resource, std::move(websocket_handshake_handler));
}

void SecureWebSocketClient::handle_websocket_handshake(const ConnectHandler &handler,
    const boost::system::error_code &ec)
{
    if (!is_connecting()) {
        return;
    }

    if (ec) {
        fail_connection(ec, handler);
        return;
    }

    set_state(ConnectionState::connected);
    if (handler) {
        handler({});
    }
    start_read();
    start_ping_timer();
}

void SecureWebSocketClient::start_read()
{
    auto self = shared_from_this();
    auto read_handler = [self](const boost::system::error_code &ec, std::size_t bytes_transferred) {
        self->handle_read(ec, bytes_transferred);
    };
    ws.async_read(read_buffer, std::move(read_handler));
}

void SecureWebSocketClient::handle_read(const boost::system::error_code &ec, std::size_t)
{
    if (!is_connected()) {
        return;
    }

    if (ec) {
        handle_unexpected_disconnect(ec);
        return;
    }

    if (message_handler) {
        const auto buffer = read_buffer.cdata();
        message_handler(static_cast<const char *>(buffer.data()), buffer.size());
    }
    read_buffer.consume(read_buffer.size());
    start_read();
}

void SecureWebSocketClient::do_write(std::string message, WriteHandler handler)
{
    if (state.load(std::memory_order_acquire) != ConnectionState::connected) {
        if (handler) {
            handler(std::make_error_code(std::errc::not_connected));
        }
        return;
    }

    write_queue.push_back(WriteRequest { std::move(message), std::move(handler) });
    if (!write_in_progress) {
        start_write();
    }
}

void SecureWebSocketClient::start_write()
{
    if (write_in_progress || write_queue.empty()) {
        return;
    }

    write_in_progress = true;
    auto self = shared_from_this();
    auto write_handler = [self](const boost::system::error_code &ec,
                             std::size_t bytes_transferred) {
        self->handle_write(ec, bytes_transferred);
    };
    ws.async_write(net::buffer(write_queue.front().message), std::move(write_handler));
}

void SecureWebSocketClient::handle_write(const boost::system::error_code &ec, std::size_t)
{
    if (!is_connected()) {
        return;
    }

    auto handler = std::move(write_queue.front().handler);
    write_queue.pop_front();
    write_in_progress = false;

    if (handler) {
        handler(static_cast<std::error_code>(ec));
    }

    if (ec) {
        handle_unexpected_disconnect(ec);
        return;
    }

    if (!write_queue.empty()) {
        start_write();
    }
}

void SecureWebSocketClient::do_close(const CloseHandler &handler)
{
    const auto current_state = state.load(std::memory_order_acquire);
    if (current_state == ConnectionState::closing) {
        if (handler) {
            handler({});
        }
        return;
    }

    if (current_state != ConnectionState::connected &&
        current_state != ConnectionState::connecting) {
        if (handler) {
            handler({});
        }
        return;
    }

    set_state(ConnectionState::closing);
    cancel_pending_operations();
    fail_pending_writes();
    close_socket();
    set_state(ConnectionState::disconnected);
    if (handler) {
        handler({});
    }
}

void SecureWebSocketClient::set_state(ConnectionState new_state)
{
    state.store(new_state, std::memory_order_release);
}

bool SecureWebSocketClient::is_connecting() const
{
    return state.load(std::memory_order_acquire) == ConnectionState::connecting;
}

void SecureWebSocketClient::cancel_pending_operations()
{
    stop_ping_timer();
    resolver.cancel();
    beast::get_lowest_layer(ws).cancel();
}

void SecureWebSocketClient::close_socket()
{
    boost::system::error_code ec;
    beast::get_lowest_layer(ws).socket().close(ec);
}

void SecureWebSocketClient::fail_pending_writes()
{
    write_in_progress = false;
    while (!write_queue.empty()) {
        auto handler = std::move(write_queue.front().handler);
        write_queue.pop_front();
        if (handler) {
            handler(std::make_error_code(std::errc::operation_canceled));
        }
    }
}

void SecureWebSocketClient::fail_connection(const boost::system::error_code &ec,
    const ConnectHandler &handler)
{
    // Disable Beast timeouts so a failed handshake does not leave a timer keeping
    // the executor alive until handshake_timeout expires.
    websocket::stream_base::timeout timeout_option {};
    timeout_option.handshake_timeout = websocket::stream_base::none();
    timeout_option.idle_timeout = websocket::stream_base::none();
    ws.set_option(timeout_option);
    cancel_pending_operations();
    close_socket();
    set_state(ConnectionState::disconnected);
    if (handler) {
        handler(static_cast<std::error_code>(ec));
    }
}

void SecureWebSocketClient::handle_unexpected_disconnect(const boost::system::error_code &ec)
{
    if (state.load(std::memory_order_acquire) != ConnectionState::connected) {
        return;
    }

    set_state(ConnectionState::disconnected);
    stop_ping_timer();
    if (disconnect_handler) {
        disconnect_handler(static_cast<std::error_code>(ec));
    }
}

void SecureWebSocketClient::start_ping_timer()
{
    if (ping_interval <= std::chrono::seconds::zero() || !is_connected()) {
        return;
    }

    ping_timer.expires_after(ping_interval);
    auto self = shared_from_this();
    ping_timer.async_wait(
        [self](const std::error_code &ec) { self->handle_ping_timer(ec); });
}

void SecureWebSocketClient::stop_ping_timer()
{
    ping_timer.cancel();
}

void SecureWebSocketClient::handle_ping_timer(const std::error_code &ec)
{
    if (ec || !is_connected() || ping_interval <= std::chrono::seconds::zero()) {
        return;
    }

    if (ping_message_generator) {
        do_write(ping_message_generator(), nullptr);
    }

    start_ping_timer();
}

} // namespace cpp_components::secure_websocket_client
