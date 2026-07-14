/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "executor.h"

#include <iostream>
#include <pthread.h>
#include <sched.h>

namespace cpp_components::executor {

Executor::Executor() :
    work(std::make_unique<boost::asio::io_context::work>(io_context))
{
    thread = std::thread([this]() {
        boost::system::error_code ec;
        io_context.run(ec);
        if (ec) {
            std::cerr << "Executor: " << ec.message() << '\n';
        }
    });
}

Executor::~Executor()
{
    stop(true);
}

void Executor::stop(bool is_force_stop)
{
    if (!thread.joinable()) {
        return;
    }

    work.reset();
    if (is_force_stop) {
        io_context.stop();
    }

    if (std::this_thread::get_id() == thread.get_id()) {
        return;
    }
    thread.join();
}

std::error_code Executor::set_priority(int priority)
{
    if (priority < 0 || priority > get_max_priority()) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (!thread.joinable()) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    sched_param sch_params {};
    sch_params.sched_priority = priority;

    const int policy = (priority == 0) ? SCHED_OTHER : SCHED_RR;
    const int ret = pthread_setschedparam(thread.native_handle(), policy, &sch_params);
    if (ret != 0) {
        return { ret, std::generic_category() };
    }

    return {};
}

std::error_code Executor::get_priority(int &priority)
{
    if (!thread.joinable()) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    int policy = 0;
    sched_param sch_params {};
    const int ret = pthread_getschedparam(thread.native_handle(), &policy, &sch_params);
    if (ret != 0) {
        return { ret, std::generic_category() };
    }

    priority = sch_params.sched_priority;
    return {};
}

int Executor::get_max_priority() const
{
    return sched_get_priority_max(SCHED_RR);
}

} // namespace cpp_components::executor