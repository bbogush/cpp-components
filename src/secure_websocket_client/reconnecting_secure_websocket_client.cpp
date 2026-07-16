/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "reconnecting_secure_websocket_client.h"

#include <algorithm>
#include <utility>

namespace cpp_components::secure_websocket_client {

std::shared_ptr<ReconnectingSecureWebSocketClient> ReconnectingSecureWebSocketClient::create(
    executor::Executor &executor)
{
    return std::shared_ptr<ReconnectingSecureWebSocketClient>(
        new ReconnectingSecureWebSocketClient(executor));
}

ReconnectingSecureWebSocketClient::ReconnectingSecureWebSocketClient(executor::Executor &executor) :
    SecureWebSocketClient(executor), reconnect_timer(executor)
{
}

ReconnectingSecureWebSocketClient::~ReconnectingSecureWebSocketClient()
{
    stop_reconnecting();
}

std::shared_ptr<ReconnectingSecureWebSocketClient> ReconnectingSecureWebSocketClient::get_self()
{
    return std::static_pointer_cast<ReconnectingSecureWebSocketClient>(shared_from_this());
}

void ReconnectingSecureWebSocketClient::connect(std::string host, std::string port,
    std::string resource, ConnectHandler handler)
{
    auto self = get_self();
    auto start_connect_handler = [self, host = std::move(host), port = std::move(port),
                                     resource = std::move(resource),
                                     handler = std::move(handler)]() mutable {
        if (self->reconnect_state.load(std::memory_order_acquire) != State::idle) {
            if (handler) {
                handler(std::make_error_code(std::errc::already_connected));
            }
            return;
        }

        self->host = std::move(host);
        self->port = std::move(port);
        self->resource = std::move(resource);
        self->connect_handler = std::move(handler);
        self->current_reconnect_delay = std::min(self->initial_reconnect_delay,
            self->max_reconnect_delay);
        self->start_connect_attempt();
    };
    executor.post(start_connect_handler);
}

void ReconnectingSecureWebSocketClient::close(CloseHandler handler)
{
    auto self = get_self();
    auto stop_reconnecting_handler = [self, handler = std::move(handler)]() mutable {
        self->stop_reconnecting();
        self->do_close(handler);
    };
    executor.post(stop_reconnecting_handler);
}

void ReconnectingSecureWebSocketClient::set_initial_reconnect_delay(
    std::chrono::steady_clock::duration delay)
{
    auto self = get_self();
    auto set_delay_handler = [self, delay]() {
        self->initial_reconnect_delay = delay;
        if (self->reconnect_state.load(std::memory_order_acquire) == State::idle) {
            self->current_reconnect_delay = delay;
        }
    };
    executor.post(set_delay_handler);
}

void ReconnectingSecureWebSocketClient::set_max_reconnect_delay(
    std::chrono::steady_clock::duration delay)
{
    auto self = get_self();
    auto set_delay_handler = [self, delay]() {
        self->max_reconnect_delay = delay;
        self->current_reconnect_delay = std::min(self->current_reconnect_delay, delay);
    };
    executor.post(set_delay_handler);
}

void ReconnectingSecureWebSocketClient::on_unexpected_disconnect(const std::error_code &ec)
{
    const bool should_reconnect = reconnect_state.load(std::memory_order_acquire) ==
        State::connected;
    SecureWebSocketClient::on_unexpected_disconnect(ec);
    if (should_reconnect) {
        schedule_reconnect();
    }
}

void ReconnectingSecureWebSocketClient::start_connect_attempt()
{
    set_reconnect_state(State::connecting);
    const auto generation = ++connection_generation;

    auto self = get_self();
    auto connect_result_handler = [self, generation](const std::error_code &ec) {
        self->handle_connect_result(generation, ec);
    };
    do_connect(connect_result_handler);
}

void ReconnectingSecureWebSocketClient::handle_connect_result(std::uint64_t generation,
    const std::error_code &ec)
{
    if (generation != connection_generation ||
        reconnect_state.load(std::memory_order_acquire) != State::connecting) {
        return;
    }

    if (ec) {
        schedule_reconnect();
        return;
    }

    current_reconnect_delay = initial_reconnect_delay;
    set_reconnect_state(State::connected);
    if (connect_handler) {
        connect_handler({});
    }
}

void ReconnectingSecureWebSocketClient::schedule_reconnect()
{
    if (!is_reconnect_active()) {
        return;
    }

    set_reconnect_state(State::waiting_reconnect);

    const auto delay = current_reconnect_delay;
    current_reconnect_delay = std::min(current_reconnect_delay * 2, max_reconnect_delay);

    const auto generation = connection_generation;
    reconnect_timer.expires_after(delay);
    auto self = get_self();
    auto reconnect_timer_handler = [self, generation](const std::error_code &ec) {
        self->handle_reconnect_timer(generation, ec);
    };
    reconnect_timer.async_wait(reconnect_timer_handler);
}

void ReconnectingSecureWebSocketClient::handle_reconnect_timer(std::uint64_t generation,
    const std::error_code &ec)
{
    if (ec || generation != connection_generation ||
        reconnect_state.load(std::memory_order_acquire) != State::waiting_reconnect) {
        return;
    }

    start_connect_attempt();
}

void ReconnectingSecureWebSocketClient::stop_reconnecting()
{
    ++connection_generation;
    set_reconnect_state(State::idle);
    reconnect_timer.cancel();
    connect_handler = nullptr;
    current_reconnect_delay = initial_reconnect_delay;
}

void ReconnectingSecureWebSocketClient::set_reconnect_state(State new_state)
{
    reconnect_state.store(new_state, std::memory_order_release);
}

bool ReconnectingSecureWebSocketClient::is_reconnect_active() const
{
    const auto current_state = reconnect_state.load(std::memory_order_acquire);
    return current_state == State::connecting || current_state == State::connected ||
        current_state == State::waiting_reconnect;
}

} // namespace cpp_components::secure_websocket_client
