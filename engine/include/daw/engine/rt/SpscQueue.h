#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <optional>
#include <type_traits>

namespace daw::engine::rt
{

// The aligned members below pad the class on purpose (MSVC warning C4324).
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable : 4324)
#endif

/**
    Bounded lock-free queue for exactly one producer thread and one consumer thread.

    All storage lives inside the object, so push() and pop() never allocate, lock or
    block: either side may be the audio thread. Capacity is fixed at compile time and
    must be a power of two.

    push() may only be called from the producer thread and pop() only from the
    consumer thread. Neither may be called concurrently with itself.
*/
template <typename T, std::size_t Capacity>
class SpscQueue
{
    static_assert(Capacity >= 2 && (Capacity & (Capacity - 1)) == 0,
                  "Capacity must be a power of two");
    static_assert(std::is_nothrow_default_constructible_v<T> &&
                      std::is_nothrow_move_constructible_v<T> &&
                      std::is_nothrow_move_assignable_v<T>,
                  "T must be movable without throwing");
    static_assert(std::atomic<std::size_t>::is_always_lock_free);

public:
    static constexpr std::size_t capacity = Capacity;

    /** Producer thread. Returns false, leaving the queue unchanged, if it is full. */
    [[nodiscard]] bool push(T item) noexcept
    {
        const auto write = writeIndex.load(std::memory_order_relaxed);
        const auto read = readIndex.load(std::memory_order_acquire);

        if (write - read == Capacity)
            return false;

        slots[write & indexMask] = std::move(item);
        writeIndex.store(write + 1, std::memory_order_release);
        return true;
    }

    /** Consumer thread. Returns the oldest item, or nothing if the queue is empty. */
    [[nodiscard]] std::optional<T> pop() noexcept
    {
        const auto read = readIndex.load(std::memory_order_relaxed);
        const auto write = writeIndex.load(std::memory_order_acquire);

        if (read == write)
            return std::nullopt;

        std::optional<T> item(std::move(slots[read & indexMask]));
        readIndex.store(read + 1, std::memory_order_release);
        return item;
    }

    /** Either thread. Only a snapshot: the other thread may change it at any time. */
    [[nodiscard]] std::size_t approximateSize() const noexcept
    {
        return writeIndex.load(std::memory_order_acquire) -
               readIndex.load(std::memory_order_acquire);
    }

private:
    static constexpr std::size_t indexMask = Capacity - 1;

    // Apple Silicon uses 128-byte cache lines; keeping the two indices apart stops
    // the producer and the consumer from invalidating each other's cache line.
    static constexpr std::size_t cacheLineSize = 128;

    // Both indices only ever increase; unsigned wrap-around keeps the arithmetic exact.
    alignas(cacheLineSize) std::atomic<std::size_t> writeIndex{0};
    alignas(cacheLineSize) std::atomic<std::size_t> readIndex{0};
    alignas(cacheLineSize) std::array<T, Capacity> slots{};
};

#if defined(_MSC_VER)
#pragma warning(pop)
#endif

} // namespace daw::engine::rt
