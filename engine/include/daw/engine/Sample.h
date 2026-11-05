#pragma once

#include "daw/engine/AudioData.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

namespace daw::engine
{

/**
    A sound an instrument plays (a sampler's sample, a drum pad's), whole in memory
    at the device's sample rate, with band-limited copies for playing it higher:
    level m holds it at a 2^m-th of the rate (filtered below that rate's Nyquist), so
    reading level m at most at its own speed never aliases, however far up the note
    goes (up to 2^(levels - 1) times the speed).

    Built off the audio thread; immutable once made, shared with the audio thread.
*/
struct Sample
{
    static constexpr int defaultLevels = 5; ///< up to 4 octaves up without aliasing

    double sampleRate = 48000.0; ///< of level 0
    std::vector<juce::AudioBuffer<float>> levels;

    [[nodiscard]] juce::int64 getLength() const noexcept
    {
        return levels.empty() ? 0 : levels.front().getNumSamples();
    }
    [[nodiscard]] int getNumChannels() const noexcept
    {
        return levels.empty() ? 0 : levels.front().getNumChannels();
    }

    /** The sample made from decoded audio already at sampleRate. Not real-time safe. */
    [[nodiscard]] static std::shared_ptr<const Sample> fromAudio(juce::AudioBuffer<float> audio,
                                                                 double sampleRate,
                                                                 int numLevels = defaultLevels);
};

} // namespace daw::engine
