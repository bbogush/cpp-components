/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "timer.h"

namespace cpp_components::timer {

Timer::Timer(executor::Executor &executor) : timer(executor.get_context())
{
}

Timer::~Timer()
{
    boost::system::error_code ec;
    timer.cancel(ec);
}

void Timer::expires_after(std::chrono::steady_clock::duration duration)
{
    timer.expires_after(duration);
}

void Timer::expires_at(std::chrono::steady_clock::time_point time_point)
{
    timer.expires_at(time_point);
}

std::chrono::steady_clock::time_point Timer::expiry() const
{
    return timer.expiry();
}

void Timer::async_wait(Handler handler)
{
    timer.async_wait([handler = std::move(handler)](const boost::system::error_code &ec) {
        if (!handler) {
            return;
        }
        handler(static_cast<std::error_code>(ec));
    });
}

void Timer::cancel()
{
    boost::system::error_code ec;
    timer.cancel(ec);
}

} // namespace cpp_components::timer
