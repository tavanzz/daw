#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <array>
#include <atomic>

namespace daw::engine::dsp
{

/**
    Measures the peak level of the audio passing through, for level meters.

    The audio thread calls process() on every block. One other thread (the UI)
    calls takePeak() at its refresh rate and gets the highest absolute sample of
    each channel since its previous call. No locks: the two threads only meet in
    one atomic float per channel.
*/
class PeakLevelMeter
{
public:
    static constexpr int maxChannels = 2;

    /** Audio thread. Measures the buffer's first numChannels channels (at most
        maxChannels). */
    void process(const juce::AudioBuffer<float>& buffer, int startSample, int numSamples,
                 int numChannels = maxChannels) noexcept;

    /** Reader thread (only one). Highest absolute sample on the channel since the
        previous call, as a linear gain; 0 for channels that are not measured. */
    [[nodiscard]] float takePeak(int channel) noexcept;

private:
    static_assert(std::atomic<float>::is_always_lock_free);

    std::array<std::atomic<float>, maxChannels> peaks{};
};

} // namespace daw::engine::dsp
