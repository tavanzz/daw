#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace daw::engine
{

/**
    The transport's de-click fade over one stretch of the timeline.

    Sample i of the stretch gets the gain (startLevel + step × (i + 1)) / length:
    step is +1 while fading in, -1 while fading out and 0 at full level (gain 1,
    nothing to do). Integer levels make every fade exact and repeatable, and the
    division (not a reciprocal multiply) gives the last fade-in sample exactly 1.

    The transport hands it to whatever renders the timeline, which applies it to the
    timeline's audio only: live input that is monitored is never faded by play,
    pause or stop.
*/
struct TimelineFade
{
    int startLevel = 1;
    int step = 0;
    int length = 1;

    [[nodiscard]] bool isFull() const noexcept { return step == 0; }

    /** The same fade, starting offset samples later. */
    [[nodiscard]] TimelineFade from(int offset) const noexcept
    {
        return {startLevel + step * offset, step, length};
    }

    /** Multiplies numSamples samples by the fade's gains. */
    void apply(float* samples, int numSamples) const noexcept
    {
        if (isFull())
            return;

        const auto fullLevel = static_cast<float>(length);
        int level = startLevel;
        for (int i = 0; i < numSamples; ++i)
        {
            level += step;
            samples[i] *= static_cast<float>(level) / fullLevel;
        }
    }

    /** The same on every channel of [start, start + numSamples). */
    void apply(juce::AudioBuffer<float>& buffer, int start, int numSamples) const noexcept
    {
        for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
            apply(buffer.getWritePointer(channel, start), numSamples);
    }
};

} // namespace daw::engine
