/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_EXECUTOR_H
#define CPP_COMPONENTS_EXECUTOR_H

#include <boost/asio.hpp>
#include <memory>
#include <system_error>
#include <thread>

namespace cpp_components::executor {

class Executor {
public:
    Executor();
    ~Executor();
    Executor(const Executor &) = delete;
    Executor(Executor &&) = delete;
    Executor &operator=(const Executor &) = delete;
    Executor &operator=(Executor &&) = delete;

    void stop(bool is_force_stop = false);
    std::error_code set_priority(int priority);
    std::error_code get_priority(int &priority);
    int get_max_priority() const;

    inline boost::asio::io_context &get_context()
    {
        return io_context;
    }

    inline const boost::asio::io_context &get_context() const
    {
        return io_context;
    }

    template<typename F>
    void post(F &&callback)
    {
        io_context.post(std::forward<F>(callback));
    }

    template<typename F>
    void dispatch(F &&callback)
    {
        io_context.dispatch(std::forward<F>(callback));
    }

private:
    boost::asio::io_context io_context;
    std::unique_ptr<boost::asio::io_context::work> work;
    std::thread thread;
};

} // namespace cpp_components::executor

#endif // CPP_COMPONENTS_EXECUTOR_H