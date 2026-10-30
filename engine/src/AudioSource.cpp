#include "daw/engine/AudioSource.h"

#include <algorithm>

namespace daw::engine
{

MemoryAudioSource::MemoryAudioSource(AudioData audioData)
    : audio(std::move(audioData))
{
}

int MemoryAudioSource::getNumChannels() const noexcept
{
    return audio.samples.getNumChannels();
}

juce::int64 MemoryAudioSource::getLength() const noexcept
{
    return audio.samples.getNumSamples();
}

double MemoryAudioSource::getSampleRate() const noexcept
{
    return audio.sampleRate;
}

bool MemoryAudioSource::read(int channel, juce::int64 start, float* destination,
                             int numSamples) const noexcept
{
    jassert(channel >= 0 && channel < getNumChannels());
    jassert(numSamples >= 0);

    const juce::int64 end = start + numSamples;
    const juce::int64 validStart = std::max(start, juce::int64{0});
    const juce::int64 validEnd = std::min(end, getLength());

    if (validEnd <= validStart)
    {
        juce::FloatVectorOperations::clear(destination, numSamples);
        return true;
    }

    // Silence before the source, its samples, silence after it.
    const auto before = static_cast<int>(validStart - start);
    const auto copied = static_cast<int>(validEnd - validStart);
    const auto after = static_cast<int>(end - validEnd);

    const float* samples = audio.samples.getReadPointer(channel, static_cast<int>(validStart));
    juce::FloatVectorOperations::clear(destination, before);
    juce::FloatVectorOperations::copy(destination + before, samples, copied);
    juce::FloatVectorOperations::clear(destination + before + copied, after);

    return true;
}

} // namespace daw::engine
