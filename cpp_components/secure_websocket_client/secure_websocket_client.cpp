/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "secure_websocket_client.h"

#include <boost/asio/bind_allocator.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/recycling_allocator.hpp>
#include <boost/asio/ssl/host_name_verification.hpp>

#include <openssl/err.h>
#include <openssl/ssl.h>

#include <algorithm>
#include <chrono>
#include <iostream>
#include <utility>

namespace cpp_components::secure_websocket_client {

namespace {

namespace beast = boost::beast;
namespace websocket = beast::websocket;
namespace net = boost::asio;
namespace ssl = net::ssl;

constexpr auto connect_timeout = std::chrono::seconds(30);
// Delay before retrying a ping that could not be queued because all write slots were in use.
constexpr auto ping_retry_delay = std::chrono::milliseconds(100);

} // namespace

std::shared_ptr<SecureWebSocketClient> SecureWebSocketClient::create(executor::Executor &executor)
{
    return std::shared_ptr<SecureWebSocketClient>(new SecureWebSocketClient(executor));
}

SecureWebSocketClient::SecureWebSocketClient(executor::Executor &executor) :
    executor(executor), ssl_context(ssl::context::tlsv12_client), resolver(executor.get_context()),
    ping_timer(executor), read_timer(executor)
{
    boost::system::error_code ec;
    ssl_context.set_default_verify_paths(ec);
    if (ec) {
        std::cerr << "SecureWebSocketClient: failed to load default CA paths: " << ec.message()
                  << '\n';
    }
    ssl_context.set_verify_mode(ssl::verify_peer, ec);
    if (ec) {
        std::cerr << "SecureWebSocketClient: failed to set verify mode: " << ec.message() << '\n';
    }
}

SecureWebSocketClient::~SecureWebSocketClient()
{
    do_close(nullptr);
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

void SecureWebSocketClient::set_read_timeout(std::chrono::seconds timeout)
{
    auto self = shared_from_this();
    auto timeout_handler = [self, timeout]() {
        self->read_timeout = timeout;
        if (!self->is_connected()) {
            return;
        }

        self->last_read_time = std::chrono::steady_clock::now();
        self->stop_read_timer();
        self->start_read_timer();
    };
    executor.post(std::move(timeout_handler));
}

bool SecureWebSocketClient::is_connected() const
{
    return state.load(std::memory_order_acquire) == ConnectionState::connected;
}

std::error_code SecureWebSocketClient::set_ca_certificate(const std::string &ca_certificate_file)
{
    if (ca_certificate_file.empty()) {
        return {};
    }

    boost::system::error_code ec;
    ssl_context.load_verify_file(ca_certificate_file, ec);
    return static_cast<std::error_code>(ec);
}

size_t SecureWebSocketClient::get_read_buffer_size()
{
    return read_buffer_size;
}

size_t SecureWebSocketClient::get_read_padding()
{
    return read_padding;
}

size_t SecureWebSocketClient::get_write_slot_size()
{
    return std::tuple_size_v<decltype(WriteSlot::data)>;
}

size_t SecureWebSocketClient::get_write_slot_count()
{
    return write_slot_count;
}

std::error_code SecureWebSocketClient::write_now(std::span<const char> message)
{
    if (!executor.get_context().get_executor().running_in_this_thread()) {
        return std::make_error_code(std::errc::operation_not_permitted);
    }

    WriteHandler no_handler;
    return queue_write(WriteType::message, message, no_handler);
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
    create_stream();

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
    if (!is_connecting() || !ws) {
        return;
    }

    if (ec) {
        fail_connection(ec, handler);
        return;
    }

    auto self = shared_from_this();
    beast::get_lowest_layer(*ws).expires_after(connect_timeout);
    auto connect_handler =
        [self, handler = std::move(handler)](const boost::system::error_code &connect_ec,
            const Tcp::endpoint &endpoint) mutable {
            self->handle_connect(std::move(handler), connect_ec, endpoint);
        };
    beast::get_lowest_layer(*ws).async_connect(results, std::move(connect_handler));
}

void SecureWebSocketClient::handle_connect(ConnectHandler handler,
    const boost::system::error_code &ec, const Tcp::endpoint &)
{
    if (!is_connecting() || !ws) {
        return;
    }

    if (ec) {
        fail_connection(ec, handler);
        return;
    }

    // Optimize write for low latency
    boost::system::error_code option_ec;
    beast::get_lowest_layer(*ws).socket().set_option(Tcp::no_delay(true), option_ec);
    if (option_ec) {
        fail_connection(option_ec, handler);
        return;
    }
    beast::get_lowest_layer(*ws).expires_after(connect_timeout);

    if (!SSL_set_tlsext_host_name(ws->next_layer().native_handle(), host.c_str())) {
        fail_connection(boost::system::error_code(static_cast<int>(::ERR_get_error()),
                            boost::asio::error::get_ssl_category()),
            handler);
        return;
    }

    boost::system::error_code verify_ec;
    ws->next_layer().set_verify_callback(ssl::host_name_verification(host), verify_ec);
    if (verify_ec) {
        fail_connection(verify_ec, handler);
        return;
    }

    auto self = shared_from_this();
    auto ssl_handshake_handler = [self, handler = std::move(handler)](
                                     const boost::system::error_code &ssl_ec) mutable {
        self->handle_ssl_handshake(std::move(handler), ssl_ec);
    };
    ws->next_layer().async_handshake(ssl::stream_base::client, std::move(ssl_handshake_handler));
}

void SecureWebSocketClient::handle_ssl_handshake(ConnectHandler handler,
    const boost::system::error_code &ec)
{
    if (!is_connecting() || !ws) {
        return;
    }

    if (ec) {
        fail_connection(ec, handler);
        return;
    }

    beast::get_lowest_layer(*ws).expires_never();
    ws->set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

    auto self = shared_from_this();
    auto websocket_handshake_handler = [self, handler = std::move(handler)](
                                           const boost::system::error_code &handshake_ec) mutable {
        self->handle_websocket_handshake(handler, handshake_ec);
    };
    ws->async_handshake(host, resource, std::move(websocket_handshake_handler));
}

void SecureWebSocketClient::handle_websocket_handshake(const ConnectHandler &handler,
    const boost::system::error_code &ec)
{
    if (!is_connecting() || !ws) {
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
    last_read_time = std::chrono::steady_clock::now();
    start_read_timer();
    start_read();
    start_ping_timer();
}

void SecureWebSocketClient::start_read()
{
    if (!ws || !is_connected()) {
        return;
    }

    auto self = shared_from_this();
    auto read_handler = [self](const boost::system::error_code &ec, std::size_t bytes_transferred) {
        self->handle_read(ec, bytes_transferred);
    };
    ws->async_read(read_buffer,
        net::bind_allocator(net::recycling_allocator<void> {}, std::move(read_handler)));
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

    last_read_time = std::chrono::steady_clock::now();
    if (message_handler) {
        const auto buffer = read_buffer.cdata();
        message_handler(static_cast<const char *>(buffer.data()), buffer.size());
    }
    read_buffer.consume(read_buffer.size());
    start_read();
}

std::error_code SecureWebSocketClient::do_write(std::string message, WriteHandler handler)
{
    const auto ec = queue_write(WriteType::message, message, handler);
    if (ec && handler) {
        handler(ec);
    }
    return ec;
}

std::error_code SecureWebSocketClient::do_protocol_ping()
{
    WriteHandler no_handler;
    const auto ec = queue_write(WriteType::ping, {}, no_handler);

    return ec;
}

std::error_code SecureWebSocketClient::queue_write(WriteType type, std::span<const char> message,
    WriteHandler &handler)
{
    if (state.load(std::memory_order_acquire) != ConnectionState::connected || !ws) {
        return std::make_error_code(std::errc::not_connected);
    }

    if (message.size() > get_write_slot_size()) {
        return std::make_error_code(std::errc::message_size);
    }

    if (write_slots.full()) {
        return std::make_error_code(std::errc::no_buffer_space);
    }

    auto &slot = write_slots.back();
    slot.type = type;
    std::copy(message.begin(), message.end(), slot.data.begin());
    slot.size = message.size();
    slot.handler = std::move(handler);
    write_slots.push_back();

    start_write();
    return {};
}

void SecureWebSocketClient::start_write()
{
    if (write_in_progress || write_slots.empty() || !ws) {
        return;
    }

    write_in_progress = true;
    auto self = shared_from_this();
    const auto &slot = write_slots.front();

    if (slot.type == WriteType::ping) {
        auto ping_handler = [self, generation = write_generation](
                                const boost::system::error_code &ec) {
            self->handle_write(generation, ec);
        };
        ws->async_ping({},
            net::bind_allocator(net::recycling_allocator<void> {}, std::move(ping_handler)));
        return;
    }

    auto write_handler = [self, generation = write_generation](const boost::system::error_code &ec,
                             std::size_t) { self->handle_write(generation, ec); };
    ws->async_write(net::buffer(slot.data.data(), slot.size),
        net::bind_allocator(net::recycling_allocator<void> {}, std::move(write_handler)));
}

void SecureWebSocketClient::handle_write(std::uint64_t generation,
    const boost::system::error_code &ec)
{
    if (generation != write_generation || !is_connected()) {
        return; // write of an earlier connection; its slot was already released
    }

    auto &slot = write_slots.front();
    auto handler = std::move(slot.handler);
    slot.handler = nullptr; // the slot is reused, so do not keep the callback alive
    write_slots.pop_front();
    write_in_progress = false;

    if (handler) {
        handler(static_cast<std::error_code>(ec));
    }

    if (ec) {
        handle_unexpected_disconnect(ec);
        return;
    }

    start_write();
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
    // Disable Beast timeouts so a cancelled handshake does not leave a timer keeping
    // the executor alive until handshake_timeout expires.
    if (ws) {
        websocket::stream_base::timeout timeout_option {};
        timeout_option.handshake_timeout = websocket::stream_base::none();
        timeout_option.idle_timeout = websocket::stream_base::none();
        ws->set_option(timeout_option);
    }
    cancel_pending_operations();
    fail_pending_writes();
    close_socket();
    // Keep ws alive since executor may have a pending read operation
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

void SecureWebSocketClient::create_stream()
{
    read_buffer.consume(read_buffer.size());
    ws = std::make_unique<WebSocketStream>(executor.get_context(), ssl_context);
    // Keeps read_padding bytes of the buffer free after every message.
    ws->read_message_max(read_buffer_size);
}

void SecureWebSocketClient::cancel_pending_operations()
{
    stop_ping_timer();
    stop_read_timer();
    resolver.cancel();
    if (ws) {
        beast::get_lowest_layer(*ws).cancel();
    }
}

void SecureWebSocketClient::close_socket()
{
    if (!ws) {
        return;
    }

    boost::system::error_code ec;
    beast::get_lowest_layer(*ws).socket().close(ec);
}

void SecureWebSocketClient::fail_pending_writes()
{
    ++write_generation;
    write_in_progress = false;
    while (!write_slots.empty()) {
        auto &slot = write_slots.front();
        auto handler = std::move(slot.handler);
        slot.handler = nullptr;
        write_slots.pop_front();
        if (handler) {
            handler(std::make_error_code(std::errc::operation_canceled));
        }
    }
}

void SecureWebSocketClient::fail_connection(const boost::system::error_code &ec,
    const ConnectHandler &handler)
{
    do_close(nullptr);
    if (handler) {
        handler(static_cast<std::error_code>(ec));
    }
}

void SecureWebSocketClient::handle_unexpected_disconnect(const boost::system::error_code &ec)
{
    if (state.load(std::memory_order_acquire) != ConnectionState::connected) {
        return;
    }

    do_close(nullptr);
    on_unexpected_disconnect(static_cast<std::error_code>(ec));
}

void SecureWebSocketClient::on_unexpected_disconnect(const std::error_code &ec)
{
    if (disconnect_handler) {
        disconnect_handler(ec);
    }
}

void SecureWebSocketClient::start_ping_timer()
{
    start_ping_timer(ping_interval);
}

void SecureWebSocketClient::start_ping_timer(std::chrono::steady_clock::duration delay)
{
    if (ping_interval <= std::chrono::seconds::zero() || !is_connected()) {
        return;
    }

    ping_timer.expires_after(delay);
    auto self = shared_from_this();
    ping_timer.async_wait([self](const std::error_code &ec) { self->handle_ping_timer(ec); });
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

    std::error_code ping_ec;
    if (ping_message_generator) {
        // Application text ping via the shared write queue
        ping_ec = do_write(ping_message_generator(), nullptr);
    } else {
        // WebSocket protocol ping frame
        ping_ec = do_protocol_ping();
    }

    // A ping not queued because all write slots were in use is retried soon instead of after a
    // full interval. Not immediately: the slots are usually full because the socket is stalled,
    // and an immediate retry would spin until a slot frees. Other errors (a generated ping
    // larger than a slot) would fail again, so they wait for the next interval.
    const bool is_slots_full = ping_ec == std::make_error_code(std::errc::no_buffer_space);
    start_ping_timer(is_slots_full ? std::chrono::steady_clock::duration { ping_retry_delay } :
                                     std::chrono::steady_clock::duration { ping_interval });
}

void SecureWebSocketClient::start_read_timer()
{
    if (read_timeout <= std::chrono::seconds::zero() || !is_connected()) {
        return;
    }

    // Armed once per timeout period, not per message: it fires when the timeout would expire
    // if no message arrived after last_read_time, and handle_read_timeout() re-arms it if one did.
    read_timer.expires_at(last_read_time + read_timeout);
    auto self = shared_from_this();
    read_timer.async_wait([self](const std::error_code &ec) { self->handle_read_timeout(ec); });
}

void SecureWebSocketClient::stop_read_timer()
{
    read_timer.cancel();
}

void SecureWebSocketClient::handle_read_timeout(const std::error_code &ec)
{
    if (ec || !is_connected() || read_timeout <= std::chrono::seconds::zero()) {
        return;
    }

    if (std::chrono::steady_clock::now() < last_read_time + read_timeout) {
        // A message arrived since the timer was armed: wait for the rest of the timeout.
        start_read_timer();
        return;
    }

    handle_unexpected_disconnect(std::make_error_code(std::errc::timed_out));
}

} // namespace cpp_components::secure_websocket_client
