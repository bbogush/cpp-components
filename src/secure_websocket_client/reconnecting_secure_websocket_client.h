/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_RECONNECTING_SECURE_WEBSOCKET_CLIENT_H
#define CPP_COMPONENTS_RECONNECTING_SECURE_WEBSOCKET_CLIENT_H

#include "secure_websocket_client.h"

#include <atomic>
#include <chrono>
#include <cstdint>
#include <memory>

namespace cpp_components::secure_websocket_client {

class ReconnectingSecureWebSocketClient : public SecureWebSocketClient {
public:
    static std::shared_ptr<ReconnectingSecureWebSocketClient> create(executor::Executor &executor);

    ReconnectingSecureWebSocketClient(const ReconnectingSecureWebSocketClient &) = delete;
    ReconnectingSecureWebSocketClient(ReconnectingSecureWebSocketClient &&) = delete;
    ReconnectingSecureWebSocketClient &operator=(
        const ReconnectingSecureWebSocketClient &) = delete;
    ReconnectingSecureWebSocketClient &operator=(ReconnectingSecureWebSocketClient &&) = delete;

    ~ReconnectingSecureWebSocketClient() override;

    void connect(std::string host, std::string port, std::string resource, ConnectHandler handler);
    void close(CloseHandler handler = nullptr);

    void set_initial_reconnect_delay(std::chrono::steady_clock::duration delay);
    void set_max_reconnect_delay(std::chrono::steady_clock::duration delay);

protected:
    void on_unexpected_disconnect(const std::error_code &ec) override;

private:
    enum class State {
        idle,
        connecting,
        connected,
        waiting_reconnect,
    };

    explicit ReconnectingSecureWebSocketClient(executor::Executor &executor);

    std::shared_ptr<ReconnectingSecureWebSocketClient> get_self();

    void start_connect_attempt();
    void handle_connect_result(std::uint64_t generation, const std::error_code &ec);
    void schedule_reconnect();
    void handle_reconnect_timer(std::uint64_t generation, const std::error_code &ec);
    void stop_reconnecting();
    void set_reconnect_state(State new_state);
    bool is_reconnect_active() const;

    timer::Timer reconnect_timer;
    ConnectHandler connect_handler;
    std::chrono::steady_clock::duration initial_reconnect_delay { std::chrono::seconds { 1 } };
    std::chrono::steady_clock::duration max_reconnect_delay { std::chrono::seconds { 60 } };
    std::chrono::steady_clock::duration current_reconnect_delay { std::chrono::seconds { 1 } };
    std::atomic<State> reconnect_state { State::idle };
    std::uint64_t connection_generation = 0;
};

} // namespace cpp_components::secure_websocket_client

#endif // CPP_COMPONENTS_RECONNECTING_SECURE_WEBSOCKET_CLIENT_H
