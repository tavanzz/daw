#pragma once

#include <atomic>
#include <cstdint>

namespace daw::engine
{

/**
    Tells another thread when the audio thread can no longer be reading memory it
    was given before: the engine marks the start and end of every audio block.

    A thread that has unpublished a pointer the audio thread reads (say, a block of
    streamed audio) asks for a ticket right after; once isPast(ticket), no audio
    block that could have seen the pointer is still running, and the memory may be
    reused. If no block was running when the ticket was taken, that is at once.
*/
class RenderClock
{
public:
    /** Audio thread, at the start and end of each block. */
    void begin() noexcept { rendering.store(true); }
    void end() noexcept
    {
        rendering.store(false);
        blocks.fetch_add(1);
    }

    /** Any thread, after unpublishing a pointer. */
    [[nodiscard]] std::uint64_t ticket() const noexcept
    {
        // Sequentially consistent: if a block that read the pointer had begun, its
        // begin() is seen here.
        return rendering.load() ? blocks.load() + 1 : blocks.load();
    }

    [[nodiscard]] bool isPast(std::uint64_t ticketToCheck) const noexcept
    {
        return blocks.load() >= ticketToCheck;
    }

private:
    std::atomic<bool> rendering{false};
    std::atomic<std::uint64_t> blocks{0};
};

} // namespace daw::engine
