/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "executor.h"

#include <iostream>

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

} // namespace cpp_components::executor