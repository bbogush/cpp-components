/*  Copyright (C) 2026 cpp-components project
 *  This program is free software; you can redistribute it and/or modify
 *  it under the terms of the Apache License Version 2.0.
 */

#include "cpp_components/fixed_ring_buffer/fixed_ring_buffer.h"

#include <gtest/gtest.h>

#include <cstddef>
#include <string>

namespace {

using cpp_components::fixed_ring_buffer::FixedRingBuffer;

template<size_t capacity>
concept ValidCapacity = requires { typename FixedRingBuffer<int, capacity>; };

static_assert(ValidCapacity<1>);
static_assert(ValidCapacity<64>);
static_assert(!ValidCapacity<0>);
static_assert(!ValidCapacity<3>);
static_assert(!ValidCapacity<48>);

// Counts constructions, to check that pushes and pops never construct or copy elements.
struct Counted {
    static inline int constructions = 0;

    Counted()
    {
        ++constructions;
    }

    Counted(const Counted &)
    {
        ++constructions;
    }

    Counted &operator=(const Counted &) = default;

    int value = 0;
};

void push(FixedRingBuffer<int, 4> &ring, int value)
{
    ring.back_slot() = value;
    ring.push_back();
}

} // namespace

TEST(FixedRingBufferTest, new_ring_is_empty)
{
    const FixedRingBuffer<int, 4> ring;
    EXPECT_TRUE(ring.empty());
    EXPECT_FALSE(ring.full());
    EXPECT_EQ(ring.size(), 0U);
    EXPECT_EQ(ring.get_capacity(), 4U);
}

TEST(FixedRingBufferTest, push_back_queues_element_filled_in_place)
{
    FixedRingBuffer<std::string, 4> ring;
    ring.back_slot() = "first";
    ring.push_back();

    EXPECT_FALSE(ring.empty());
    EXPECT_EQ(ring.size(), 1U);
    EXPECT_EQ(ring.front(), "first");
}

TEST(FixedRingBufferTest, is_full_at_capacity)
{
    FixedRingBuffer<int, 4> ring;
    for (int i = 0; i < 4; ++i) {
        EXPECT_FALSE(ring.full());
        push(ring, i);
    }

    EXPECT_TRUE(ring.full());
    EXPECT_EQ(ring.size(), 4U);

    ring.pop_front();
    EXPECT_FALSE(ring.full());
    EXPECT_EQ(ring.size(), 3U);
}

TEST(FixedRingBufferTest, pops_in_fifo_order)
{
    FixedRingBuffer<int, 4> ring;
    for (int i = 0; i < 4; ++i) {
        push(ring, i);
    }

    for (int i = 0; i < 4; ++i) {
        EXPECT_EQ(ring.front(), i);
        ring.pop_front();
    }
    EXPECT_TRUE(ring.empty());
}

TEST(FixedRingBufferTest, keeps_fifo_order_while_positions_wrap_around)
{
    FixedRingBuffer<int, 4> ring;
    int next_push = 0;
    int next_pop = 0;

    // Push three, pop two, repeatedly: queued elements straddle the end of the storage.
    for (int round = 0; round < 1000; ++round) {
        while (!ring.full() && next_push < next_pop + 3) {
            push(ring, next_push++);
        }
        for (int i = 0; i < 2 && !ring.empty(); ++i) {
            ASSERT_EQ(ring.front(), next_pop++);
            ring.pop_front();
        }
    }

    while (!ring.empty()) {
        ASSERT_EQ(ring.front(), next_pop++);
        ring.pop_front();
    }
    EXPECT_EQ(next_pop, next_push);
}

TEST(FixedRingBufferTest, queued_element_does_not_move_while_others_are_pushed)
{
    FixedRingBuffer<int, 4> ring;
    push(ring, 7);
    const int *front = &ring.front();

    push(ring, 8);
    push(ring, 9);
    push(ring, 10);

    EXPECT_EQ(&ring.front(), front);
    EXPECT_EQ(*front, 7);
}

TEST(FixedRingBufferTest, pop_front_does_not_reset_element)
{
    // Capacity 1: the next push reuses the element just popped.
    FixedRingBuffer<std::string, 1> ring;
    ring.back_slot() = "kept";
    ring.push_back();
    ring.pop_front();

    EXPECT_EQ(ring.back_slot(), "kept");
}

TEST(FixedRingBufferTest, push_and_pop_do_not_construct_elements)
{
    Counted::constructions = 0;
    FixedRingBuffer<Counted, 8> ring;
    EXPECT_EQ(Counted::constructions, 8);

    for (int i = 0; i < 100; ++i) {
        ring.back_slot().value = i;
        ring.push_back();
        EXPECT_EQ(ring.front().value, i);
        ring.pop_front();
    }

    EXPECT_EQ(Counted::constructions, 8);
}

TEST(FixedRingBufferTest, const_front_returns_oldest_element)
{
    FixedRingBuffer<int, 4> ring;
    push(ring, 1);
    push(ring, 2);

    const auto &const_ring = ring;
    EXPECT_EQ(const_ring.front(), 1);
}
