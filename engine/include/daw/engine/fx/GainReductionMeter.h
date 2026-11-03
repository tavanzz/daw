#pragma once

#include <atomic>

namespace daw::engine::fx
{

/**
    The largest gain reduction a dynamics effect applied since the reader last
    looked, in decibels (0 or more). The audio thread reports every block; one other
    thread (the UI) takes it at its refresh rate. Lock-free: one atomic float.
*/
class GainReductionMeter
{
public:
    /** Audio thread. */
    void report(float decibels) noexcept
    {
        if (decibels > largest.load(std::memory_order_relaxed))
            largest.store(decibels, std::memory_order_relaxed);
    }

    /** Reader thread (only one). */
    [[nodiscard]] float take() noexcept { return largest.exchange(0.0f, std::memory_order_relaxed); }

private:
    static_assert(std::atomic<float>::is_always_lock_free);
    std::atomic<float> largest{0.0f};
};

} // namespace daw::engine::fx
