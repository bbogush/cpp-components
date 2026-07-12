/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_TIMER_H
#define CPP_COMPONENTS_TIMER_H

#include "executor/executor.h"

#include <boost/asio/steady_timer.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <system_error>

namespace cpp_components::timer {

class Timer {
public:
    using Handler = std::function<void(const std::error_code &ec)>;

    explicit Timer(executor::Executor &executor);
    ~Timer();

    Timer(const Timer &) = delete;
    Timer(Timer &&) = delete;
    Timer &operator=(const Timer &) = delete;
    Timer &operator=(Timer &&) = delete;

    void expires_after(std::chrono::steady_clock::duration duration);
    void expires_at(std::chrono::steady_clock::time_point time_point);
    std::chrono::steady_clock::time_point expiry() const;

    void async_wait(Handler handler);
    void cancel();

private:
    boost::asio::steady_timer timer;
};

} // namespace cpp_components::timer

#endif // CPP_COMPONENTS_TIMER_H
