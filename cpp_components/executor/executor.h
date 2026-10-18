/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_EXECUTOR_H
#define CPP_COMPONENTS_EXECUTOR_H

#include <boost/asio.hpp>

#include <pthread.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <system_error>
#include <thread>

namespace cpp_components::executor {

class Executor {
public:
    // Drains the caller's queues on the executor thread. Returns true if work remains; the
    // handler is then called again after other ready handlers run.
    using DoorbellHandler = std::function<bool()>;

    // Executor without a doorbell; ring() is a no-op.
    Executor();
    // Executor that runs doorbell_handler on its thread after ring(). If the doorbell cannot
    // be set up, the error is logged, returned by get_doorbell_error(), and ring() is a no-op.
    explicit Executor(DoorbellHandler doorbell_handler);
    ~Executor();
    Executor(const Executor &) = delete;
    Executor(Executor &&) = delete;
    Executor &operator=(const Executor &) = delete;
    Executor &operator=(Executor &&) = delete;

    void stop(bool is_force_stop = false);
    std::error_code set_priority(int priority);
    std::error_code get_priority(int &priority);
    std::error_code set_cpu_affinity(int cpu_id);
    int get_max_priority() const;

    // Error from setting up the doorbell in the constructor; empty if it is set up or the
    // executor has no doorbell handler.
    std::error_code get_doorbell_error() const;

    // Wakes the executor to run the doorbell handler; call after pushing to a queue the
    // handler drains. Thread-safe, wait-free, no allocation. Writes to an eventfd only when
    // the executor is waiting on the doorbell, so a busy executor costs no syscall.
    // No-op without a doorbell handler.
    void ring() noexcept;

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
        io_context.post(boost::asio::bind_allocator(boost::asio::recycling_allocator<void> {},
            std::forward<F>(callback)));
    }

    template<typename F>
    void dispatch(F &&callback)
    {
        io_context.dispatch(boost::asio::bind_allocator(boost::asio::recycling_allocator<void> {},
            std::forward<F>(callback)));
    }

private:
    void setup_doorbell();
    void start_doorbell_wait();
    void handle_doorbell(const boost::system::error_code &ec);
    void run_doorbell_handler();
    void write_doorbell() const noexcept;

    boost::asio::io_context io_context;
    // Declared after io_context so it is destroyed (and its eventfd closed) first.
    boost::asio::posix::stream_descriptor doorbell;
    std::unique_ptr<boost::asio::io_context::work> work;
    std::thread thread;

    // Set in the constructor before the thread starts and never changed, so every thread
    // reads them without synchronization.
    pthread_t thread_handle {};
    DoorbellHandler doorbell_handler;
    int doorbell_fd = -1;
    std::error_code doorbell_error;

    std::atomic<bool> is_stopping { false };
    std::atomic<bool> doorbell_armed { false };
    uint64_t doorbell_counter = 0; // executor thread only
};

} // namespace cpp_components::executor

#endif // CPP_COMPONENTS_EXECUTOR_H
