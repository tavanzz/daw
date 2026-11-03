#pragma once

#include "daw/engine/Arrangement.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

namespace daw::test
{

inline std::shared_ptr<const engine::MemoryAudioSource> makeSource(juce::AudioBuffer<float> samples,
                                                                   double sampleRate)
{
    engine::AudioData data;
    data.sampleRate = sampleRate;
    data.samples = std::move(samples);
    return std::make_shared<const engine::MemoryAudioSource>(std::move(data));
}

inline std::shared_ptr<const engine::MemoryAudioSource> makeConstantSource(int numChannels,
                                                                           int length, float value,
                                                                           double sampleRate)
{
    juce::AudioBuffer<float> samples(numChannels, length);
    for (int channel = 0; channel < numChannels; ++channel)
        juce::FloatVectorOperations::fill(samples.getWritePointer(channel), value, length);
    return makeSource(std::move(samples), sampleRate);
}

/** One track (id trackId) playing the whole source from the start of the timeline. */
inline std::unique_ptr<engine::Arrangement>
makeWholeFileArrangement(const std::shared_ptr<const engine::MemoryAudioSource>& source,
                         juce::int64 trackId = 1, float gain = 1.0f)
{
    auto arrangement = std::make_unique<engine::Arrangement>();
    arrangement->sampleRate = source->getSampleRate();
    arrangement->length = source->getLength();

    engine::ArrangementTrack track;
    track.id = trackId;
    track.leftGain = gain;
    track.rightGain = gain;
    track.segments.push_back({source.get(), 0, source->getLength(), 0, 0, 0});
    arrangement->tracks.push_back(std::move(track));
    arrangement->sources.push_back(source);
    return arrangement;
}

/** A sample exactly representable in 24 bits, different for every channel and
    stream index: device input as a 24-bit converter delivers it. */
inline float testInputSample(int channel, juce::int64 index)
{
    auto x = static_cast<juce::uint64>(index) * 64u + static_cast<juce::uint64>(channel) +
             0x9e3779b97f4a7c15ull;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ull;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebull;
    x ^= x >> 31;
    const auto value = static_cast<int>(x & 0xffffffu) - (1 << 23); // -2^23 to 2^23 - 1
    return static_cast<float>(value) / static_cast<float>(1 << 23);
}

} // namespace daw::test
