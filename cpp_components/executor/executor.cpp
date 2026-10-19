/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "executor.h"

#include <cerrno>
#include <cstdlib>
#include <exception>
#include <iostream>
#include <pthread.h>
#include <sched.h>
#include <sys/eventfd.h>
#include <unistd.h>

namespace cpp_components::executor {

Executor::Executor() : Executor(DoorbellHandler {})
{
}

Executor::Executor(DoorbellHandler doorbell_handler) :
    // CONCURRENCY_HINT_1 - avoid redundant locking if only one thread runs the io_context
    io_context(BOOST_ASIO_CONCURRENCY_HINT_1), doorbell(io_context),
    work(std::make_unique<boost::asio::io_context::work>(io_context)),
    doorbell_handler(std::move(doorbell_handler))
{
}

std::error_code Executor::start()
{
    if (is_stopping.load(std::memory_order_acquire) ||
        is_start_called.exchange(true, std::memory_order_acq_rel)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    // Before the thread starts, so nothing races with the doorbell setup.
    if (doorbell_handler) {
        if (const auto ec = setup_doorbell()) {
            return ec;
        }
    }

    // std::thread reports a failure to create the thread only by throwing; keep it here.
    try {
        thread = std::thread([this]() { run(); });
    } catch (const std::system_error &e) {
        return e.code();
    }

    thread_handle = thread.native_handle();
    is_started.store(true, std::memory_order_release);
    return {};
}

void Executor::run() noexcept
{
    // Components do not throw, but a callback might. An exception escaping a handler would
    // leave its operation half done, so log it and terminate on purpose instead of letting
    // it reach std::terminate without a message.
    try {
        boost::system::error_code ec;
        io_context.run(ec);
        if (ec) {
            std::cerr << "Executor: " << ec.message() << '\n';
        }
    } catch (const std::exception &e) {
        std::cerr << "Executor: unhandled exception in a callback: " << e.what() << '\n';
        std::abort();
    } catch (...) {
        std::cerr << "Executor: unhandled unknown exception in a callback\n";
        std::abort();
    }
}

Executor::~Executor()
{
    stop(true);
}

void Executor::stop(bool is_force_stop)
{
    // Set first: the doorbell handler sees it and does not start another wait.
    is_stopping.store(true, std::memory_order_release);

    if (!thread.joinable()) {
        return;
    }

    if (doorbell_fd >= 0) {
        // The pending doorbell wait is outstanding work; cancel it so run() can return.
        boost::asio::post(io_context, [this]() {
            doorbell_armed.store(false, std::memory_order_relaxed);
            boost::system::error_code ec;
            doorbell.cancel(ec);
        });
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

    if (!is_started.load(std::memory_order_acquire) ||
        is_stopping.load(std::memory_order_acquire)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    sched_param sch_params {};
    sch_params.sched_priority = priority;

    const int policy = (priority == 0) ? SCHED_OTHER : SCHED_RR;
    const int ret = pthread_setschedparam(thread_handle, policy, &sch_params);
    if (ret != 0) {
        return { ret, std::generic_category() };
    }

    return {};
}

std::error_code Executor::get_priority(int &priority)
{
    if (!is_started.load(std::memory_order_acquire) ||
        is_stopping.load(std::memory_order_acquire)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    int policy = 0;
    sched_param sch_params {};
    const int ret = pthread_getschedparam(thread_handle, &policy, &sch_params);
    if (ret != 0) {
        return { ret, std::generic_category() };
    }

    priority = sch_params.sched_priority;
    return {};
}

std::error_code Executor::set_cpu_affinity(int cpu_id)
{
    if (cpu_id < 0) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    if (!is_started.load(std::memory_order_acquire) ||
        is_stopping.load(std::memory_order_acquire)) {
        return std::make_error_code(std::errc::invalid_argument);
    }

    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);

    const int ret = pthread_setaffinity_np(thread_handle, sizeof(cpu_set_t), &cpuset);
    if (ret != 0) {
        return { ret, std::generic_category() };
    }

    return {};
}

int Executor::get_max_priority() const
{
    return sched_get_priority_max(SCHED_RR);
}

void Executor::ring() noexcept
{
    // Both sides use read-modify-write on doorbell_armed, so they are totally ordered:
    // if this exchange comes after the executor arms, it reads true and wakes the executor;
    // if it comes before, the executor's arming exchange acquires it and the handler sees
    // the item pushed before this call.
    if (doorbell_armed.exchange(false, std::memory_order_acq_rel)) {
        write_doorbell();
    }
}

std::error_code Executor::setup_doorbell()
{
    const int fd = ::eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    if (fd < 0) {
        return { errno, std::generic_category() };
    }

    boost::system::error_code ec;
    doorbell.assign(fd, ec);
    if (ec) {
        ::close(fd);
        return static_cast<std::error_code>(ec);
    }

    // The descriptor owns the eventfd from here and closes it on destruction.
    doorbell_fd = fd;
    // Armed from the start: a ring() before the first wait writes the eventfd, and the wait
    // completes as soon as the thread runs it.
    doorbell_armed.store(true, std::memory_order_release);
    start_doorbell_wait();
    return {};
}

void Executor::start_doorbell_wait()
{
    doorbell.async_read_some(boost::asio::buffer(&doorbell_counter, sizeof(doorbell_counter)),
        boost::asio::bind_allocator(boost::asio::recycling_allocator<void> {},
            [this](const boost::system::error_code &ec, std::size_t) { handle_doorbell(ec); }));
}

void Executor::handle_doorbell(const boost::system::error_code &ec)
{
    if (ec) {
        if (ec != boost::asio::error::operation_aborted) {
            std::cerr << "Executor doorbell: " << ec.message() << '\n';
        }
        doorbell_armed.store(false, std::memory_order_relaxed);
        return;
    }

    // Once stopping, pending items are not handled and no new wait is started (the cancel
    // posted by stop() may have run already).
    if (is_stopping.load(std::memory_order_acquire)) {
        doorbell_armed.store(false, std::memory_order_relaxed);
        return;
    }

    run_doorbell_handler();
    // The handler itself may have called stop().
    if (!is_stopping.load(std::memory_order_acquire)) {
        start_doorbell_wait();
    }
}

void Executor::run_doorbell_handler()
{
    // Arm before draining, so an item pushed after the handler's last check rings again.
    // An exchange, not a store: see ring().
    doorbell_armed.exchange(true, std::memory_order_acq_rel);

    if (doorbell_handler()) {
        // Work remains: come back after other ready handlers instead of starving them.
        doorbell_armed.store(false, std::memory_order_relaxed);
        write_doorbell();
    }
}

void Executor::write_doorbell() const noexcept
{
    const uint64_t one = 1;
    // eventfd write fails only if the counter would overflow, which ring() cannot reach,
    // so a failure is ignored. The result must still be used: write() is warn_unused_result.
    if (::write(doorbell_fd, &one, sizeof(one)) < 0) {
        return;
    }
}

} // namespace cpp_components::executor
