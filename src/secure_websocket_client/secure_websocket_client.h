/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_SECURE_WEBSOCKET_CLIENT_H
#define CPP_COMPONENTS_SECURE_WEBSOCKET_CLIENT_H

#include "executor/executor.h"
#include "timer/timer.h"

#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/ssl.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>

#include <atomic>
#include <chrono>
#include <cstddef>
#include <deque>
#include <functional>
#include <memory>
#include <string>
#include <system_error>

namespace cpp_components::secure_websocket_client {

class SecureWebSocketClient : public std::enable_shared_from_this<SecureWebSocketClient> {
public:
    using ConnectHandler = std::function<void(const std::error_code &ec)>;
    using MessageHandler = std::function<void(const char *data, size_t size)>;
    using WriteHandler = std::function<void(const std::error_code &ec)>;
    using CloseHandler = std::function<void(const std::error_code &ec)>;
    using DisconnectHandler = std::function<void(const std::error_code &ec)>;
    using PingMessageGenerator = std::function<std::string()>;

    static std::shared_ptr<SecureWebSocketClient> create(executor::Executor &executor);

    SecureWebSocketClient(const SecureWebSocketClient &) = delete;
    SecureWebSocketClient(SecureWebSocketClient &&) = delete;
    SecureWebSocketClient &operator=(const SecureWebSocketClient &) = delete;
    SecureWebSocketClient &operator=(SecureWebSocketClient &&) = delete;

    ~SecureWebSocketClient();

    void connect(std::string host, std::string port, std::string resource, ConnectHandler handler);
    void write(std::string message, WriteHandler handler);
    void close(CloseHandler handler = nullptr);

    void set_message_handler(MessageHandler handler);
    void set_disconnect_handler(DisconnectHandler handler);
    void set_ping_message_generator(PingMessageGenerator generator);
    void set_ping_interval(std::chrono::seconds interval);
    void set_read_timeout(std::chrono::seconds timeout);

    bool is_connected() const;

    void set_ca_certificate(const std::string &ca_certificate_file);

private:
    using Tcp = boost::asio::ip::tcp;
    using WebSocketStream =
        boost::beast::websocket::stream<boost::asio::ssl::stream<boost::beast::tcp_stream>>;

    enum class ConnectionState {
        disconnected,
        connecting,
        connected,
        closing,
    };

    struct WriteRequest {
        std::string message;
        WriteHandler handler;
    };

    explicit SecureWebSocketClient(executor::Executor &executor);

    void do_connect(ConnectHandler handler);
    void handle_resolve(ConnectHandler handler, const boost::system::error_code &ec,
        const Tcp::resolver::results_type &results);
    void handle_connect(ConnectHandler handler, const boost::system::error_code &ec,
        const Tcp::endpoint &endpoint);
    void handle_ssl_handshake(ConnectHandler handler, const boost::system::error_code &ec);
    void handle_websocket_handshake(const ConnectHandler &handler,
        const boost::system::error_code &ec);

    void start_read();
    void handle_read(const boost::system::error_code &ec, std::size_t bytes_transferred);

    void do_write(std::string message, WriteHandler handler);
    void start_write();
    void handle_write(const boost::system::error_code &ec, std::size_t bytes_transferred);

    void do_close(const CloseHandler &handler);

    void set_state(ConnectionState new_state);
    bool is_connecting() const;
    void cancel_pending_operations();
    void close_socket();
    void fail_pending_writes();
    void fail_connection(const boost::system::error_code &ec, const ConnectHandler &handler);
    void handle_unexpected_disconnect(const boost::system::error_code &ec);

    void start_ping_timer();
    void stop_ping_timer();
    void handle_ping_timer(const std::error_code &ec);

    void start_read_timer();
    void stop_read_timer();
    void handle_read_timeout(const std::error_code &ec);

    executor::Executor &executor;
    boost::asio::ssl::context ssl_context;
    Tcp::resolver resolver;
    WebSocketStream ws;
    timer::Timer ping_timer;
    timer::Timer read_timer;
    boost::beast::flat_buffer read_buffer;
    std::string host;
    std::string port;
    std::string resource;
    std::deque<WriteRequest> write_queue;
    bool write_in_progress = false;
    std::atomic<ConnectionState> state { ConnectionState::disconnected };
    MessageHandler message_handler;
    DisconnectHandler disconnect_handler;
    PingMessageGenerator ping_message_generator;
    std::chrono::seconds ping_interval { 0 };
    std::chrono::seconds read_timeout { 0 };
};

} // namespace cpp_components::secure_websocket_client

#endif // CPP_COMPONENTS_SECURE_WEBSOCKET_CLIENT_H
