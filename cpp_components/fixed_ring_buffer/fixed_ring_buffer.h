/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_FIXED_RING_BUFFER_H
#define CPP_COMPONENTS_FIXED_RING_BUFFER_H

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>
#include <utility>

namespace cpp_components::fixed_ring_buffer {

// Fixed-capacity FIFO queue that never allocates, moves, or copies its elements.
//
// All capacity elements are default-constructed once, together with the FixedRingBuffer, and are
// reused: a push fills the next free element in place (get_back_slot(), then push_back()), and a
// pop only releases it. Elements are not destroyed or reset on pop, so the caller resets what
// must not outlive the entry (for example a callback). Elements do not move while queued, so a
// reference to a queued element stays valid until it is popped, even while others are pushed.
//
// Not thread-safe: use from one thread only.
template<typename T, size_t capacity>
    requires(std::has_single_bit(capacity))
class FixedRingBuffer {
public:
    static constexpr size_t get_capacity() noexcept
    {
        return capacity;
    }

    bool is_empty() const noexcept
    {
        return head == tail;
    }

    bool is_full() const noexcept
    {
        return get_size() == capacity;
    }

    size_t get_size() const noexcept
    {
        return static_cast<size_t>(tail - head);
    }

    T &get_back_slot() noexcept
    {
        assert(!is_full());
        return buffer[get_index(tail)];
    }

    void push_back() noexcept
    {
        assert(!is_full());
        ++tail;
    }

    void push_back(const T &value) noexcept
    {
        assert(!is_full());
        buffer[get_index(tail)] = value;
        ++tail;
    }

    void push_back(T &&value) noexcept
    {
        assert(!is_full());
        buffer[get_index(tail)] = std::move(value);
        ++tail;
    }

    T &get_front() noexcept
    {
        assert(!is_empty());
        return buffer[get_index(head)];
    }

    const T &get_front() const noexcept
    {
        assert(!is_empty());
        return buffer[get_index(head)];
    }

    void pop_front() noexcept
    {
        assert(!is_empty());
        ++head;
    }

private:
    static constexpr size_t get_index(uint64_t pos) noexcept
    {
        return static_cast<size_t>(pos & (capacity - 1));
    }

    std::array<T, capacity> buffer {};
    uint64_t head = 0; // oldest element index
    uint64_t tail = 0; // next free element index
};

} // namespace cpp_components::fixed_ring_buffer

#endif // CPP_COMPONENTS_FIXED_RING_BUFFER_H
