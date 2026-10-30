#include "daw/engine/rt/SpscQueue.h"

#include "support/RealtimeCheck.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <thread>

using daw::engine::rt::SpscQueue;

TEST_CASE("SpscQueue starts empty", "[engine][rt]")
{
    SpscQueue<int, 8> queue;

    CHECK(queue.approximateSize() == 0);
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("SpscQueue returns items in order and refuses pushes when full", "[engine][rt]")
{
    SpscQueue<int, 4> queue;

    CHECK(queue.push(1));
    CHECK(queue.push(2));
    CHECK(queue.push(3));
    CHECK(queue.push(4));
    CHECK_FALSE(queue.push(5));
    CHECK(queue.approximateSize() == 4);

    CHECK(queue.pop() == 1);
    CHECK(queue.pop() == 2);
    CHECK(queue.push(5));
    CHECK(queue.pop() == 3);
    CHECK(queue.pop() == 4);
    CHECK(queue.pop() == 5);
    CHECK_FALSE(queue.pop().has_value());
}

TEST_CASE("SpscQueue keeps the order across many wrap-arounds", "[engine][rt]")
{
    SpscQueue<int, 8> queue;
    int nextToPush = 0;
    int nextExpected = 0;
    bool inOrder = true;

    // Uneven push/pop batches move the indices through every slot position.
    for (int round = 0; round < 1000; ++round)
    {
        for (int i = 0; i < 1 + round % 7; ++i)
            if (queue.push(nextToPush))
                ++nextToPush;

        for (int i = 0; i < 1 + (round + 3) % 5; ++i)
            if (const auto item = queue.pop())
                inOrder = inOrder && (*item == nextExpected++);
    }

    while (const auto item = queue.pop())
        inOrder = inOrder && (*item == nextExpected++);

    CHECK(inOrder);
    CHECK(nextExpected == nextToPush);
    CHECK(nextToPush > 1000);
}

TEST_CASE("SpscQueue push and pop are real-time safe", "[engine][rt]")
{
    SpscQueue<std::int64_t, 64> queue;
    std::int64_t sum = 0;

    const auto heapOperations = daw::test::heapOperationsDuring([&] {
        for (std::int64_t i = 0; i < 64; ++i)
            (void) queue.push(i);
        while (const auto item = queue.pop())
            sum += *item;
    });

    CHECK(heapOperations == 0);
    CHECK(sum == 63 * 64 / 2);
}

namespace
{
// Two halves that must always match: a torn read or a lost write breaks the pair.
struct Message
{
    std::uint64_t value = 0;
    std::uint64_t complement = ~std::uint64_t{0};
};
} // namespace

TEST_CASE("SpscQueue delivers every item exactly once between two threads", "[engine][rt]")
{
    constexpr std::uint64_t itemCount = 200'000;
    SpscQueue<Message, 64> queue;

    std::thread producer([&] {
        for (std::uint64_t i = 0; i < itemCount; ++i)
            while (!queue.push(Message{i, ~i}))
                std::this_thread::yield();
    });

    std::uint64_t received = 0;
    bool intact = true;

    while (received < itemCount)
    {
        if (const auto message = queue.pop())
        {
            intact = intact && message->value == received && message->complement == ~received;
            ++received;
        }
        else
        {
            std::this_thread::yield();
        }
    }

    producer.join();

    CHECK(intact);
    CHECK(received == itemCount);
    CHECK_FALSE(queue.pop().has_value());
}
