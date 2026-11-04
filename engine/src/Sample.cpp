#include "daw/engine/Sample.h"

#include "daw/engine/dsp/Resampler.h"

namespace daw::engine
{

std::shared_ptr<const Sample> Sample::fromAudio(juce::AudioBuffer<float> audio, double sampleRate,
                                                int numLevels)
{
    auto sample = std::make_shared<Sample>();
    sample->sampleRate = sampleRate;
    sample->levels.push_back(std::move(audio));

    // Each level from the one before, at half its rate.
    for (int level = 1; level < numLevels; ++level)
    {
        const auto& previous = sample->levels.back();
        if (previous.getNumSamples() < 16)
            break;
        const double rate = sampleRate / static_cast<double>(1 << (level - 1));
        sample->levels.push_back(dsp::resample(previous, rate, rate / 2.0));
    }
    return sample;
}

} // namespace daw::engine
