#pragma once

#include "daw/engine/AudioInputs.h"

#include <array>
#include <atomic>

namespace daw::engine::dsp
{

/**
    Peak levels of every device input channel, before any processing: what the
    input meters show, whether or not anything records or monitors the channel.

    Like PeakLevelMeter, but for up to AudioInputs::maxChannels mono channels: the
    audio thread calls process() on every block, one reader thread takes the peaks.
*/
class InputMeters
{
public:
    /** Audio thread. */
    void process(const AudioInputs& inputs, int numSamples) noexcept;

    /** Reader thread (only one). Highest absolute sample on the device input channel
        since the previous call, as a linear gain; 0 for channels that are off. */
    [[nodiscard]] float takePeak(int channel) noexcept;

private:
    static_assert(std::atomic<float>::is_always_lock_free);

    std::array<std::atomic<float>, AudioInputs::maxChannels> peaks{};
};

} // namespace daw::engine::dsp
