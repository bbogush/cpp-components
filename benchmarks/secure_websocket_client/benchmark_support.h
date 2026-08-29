/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_BENCHMARKS_SECURE_WEBSOCKET_CLIENT_BENCHMARK_SUPPORT_H
#define CPP_COMPONENTS_BENCHMARKS_SECURE_WEBSOCKET_CLIENT_BENCHMARK_SUPPORT_H

#include "cpp_components/secure_websocket_client/secure_websocket_client.h"

#include <chrono>
#include <cstdint>
#include <future>
#include <string>
#include <string_view>

namespace secure_websocket_client_benchmark {

constexpr auto wait_timeout = std::chrono::seconds { 5 };
constexpr std::string_view benchmark_payload = "ping";

template<typename T>
bool wait_ready(const std::shared_future<T> &future)
{
    return future.wait_for(wait_timeout) == std::future_status::ready;
}

uint16_t start_secure_echo_server();
uint16_t start_secure_push_server();

bool connect_client(cpp_components::secure_websocket_client::SecureWebSocketClient &client,
    const std::string &port_string);

void close_client(cpp_components::secure_websocket_client::SecureWebSocketClient &client);

} // namespace secure_websocket_client_benchmark

#endif
