#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace daw::engine
{

/**
    Top-level audio engine.

    Threading:
    - prepare() runs on the message thread and may allocate.
    - process() runs on the audio thread and must be real-time safe:
      no allocation, no locks, no I/O, no logging, no blocking calls.
*/
class Engine
{
public:
    /** Message thread. Call before process() and whenever the output device's
        sample rate or maximum block size changes. */
    void prepare(double newSampleRate, int newMaximumBlockSize);

    /** Audio thread. Overwrites the whole buffer with the next block of output. */
    void process(juce::AudioBuffer<float>& output) noexcept;

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] int getMaximumBlockSize() const noexcept { return maximumBlockSize; }

private:
    double sampleRate = 0.0;
    int maximumBlockSize = 0;
};

} // namespace daw::engine
