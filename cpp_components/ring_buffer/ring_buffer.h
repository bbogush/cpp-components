/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#ifndef CPP_COMPONENTS_RING_BUFFER_H
#define CPP_COMPONENTS_RING_BUFFER_H

#include <array>
#include <bit>
#include <cassert>
#include <cstddef>
#include <cstdint>

namespace cpp_components::ring_buffer {

// Fixed-capacity FIFO queue that never allocates, moves, or copies its elements.
//
// All capacity elements are default-constructed once, together with the RingBuffer, and are
// reused: a push fills the next free element in place (back(), then push_back()), and a
// pop only releases it. Elements are not destroyed or reset on pop, so the caller resets what
// must not outlive the entry (for example a callback). Elements do not move while queued, so a
// reference to a queued element stays valid until it is popped, even while others are pushed.
//
// Not thread-safe: use from one thread only.
template<typename T, std::size_t capacity>
    requires(std::has_single_bit(capacity))
class RingBuffer {
public:
    static constexpr std::size_t get_capacity() noexcept
    {
        return capacity;
    }

    bool empty() const noexcept
    {
        return head == tail;
    }

    bool full() const noexcept
    {
        return size() == capacity;
    }

    std::size_t size() const noexcept
    {
        // Correct across wrap-around of the 64-bit indices.
        return static_cast<std::size_t>(tail - head);
    }

    // Next free element, to fill in place before push_back(). Requires !full().
    T &back() noexcept
    {
        assert(!full());
        return elements[index(tail)];
    }

    // Queues the element filled through back(). Requires !full().
    void push_back() noexcept
    {
        assert(!full());
        ++tail;
    }

    // Oldest queued element. Requires !empty().
    T &front() noexcept
    {
        assert(!empty());
        return elements[index(head)];
    }

    const T &front() const noexcept
    {
        assert(!empty());
        return elements[index(head)];
    }

    // Releases the oldest queued element. Requires !empty().
    void pop_front() noexcept
    {
        assert(!empty());
        ++head;
    }

private:
    // capacity is a power of two, so this mapping stays continuous when the indices wrap.
    static constexpr std::size_t index(std::uint64_t position) noexcept
    {
        return static_cast<std::size_t>(position & (capacity - 1));
    }

    std::array<T, capacity> elements {};
    std::uint64_t head = 0; // position of the oldest queued element
    std::uint64_t tail = 0; // position of the next free element
};

} // namespace cpp_components::ring_buffer

#endif // CPP_COMPONENTS_RING_BUFFER_H
