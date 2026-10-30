#include "daw/engine/dsp/WaveformPeaks.h"

#include <algorithm>

namespace daw::engine::dsp
{

WaveformPeaks WaveformPeaks::compute(const juce::AudioBuffer<float>& audio, int samplesPerBucket)
{
    jassert(samplesPerBucket > 0);

    WaveformPeaks peaks;
    peaks.numChannels = audio.getNumChannels();
    peaks.numSamples = audio.getNumSamples();
    peaks.samplesPerBucket = std::max(1, samplesPerBucket);
    peaks.numBuckets = (audio.getNumSamples() + peaks.samplesPerBucket - 1) / peaks.samplesPerBucket;
    peaks.buckets.resize(static_cast<std::size_t>(peaks.numChannels) *
                         static_cast<std::size_t>(peaks.numBuckets));

    for (int channel = 0; channel < peaks.numChannels; ++channel)
    {
        for (int bucket = 0; bucket < peaks.numBuckets; ++bucket)
        {
            const int start = bucket * peaks.samplesPerBucket;
            const int length = std::min(peaks.samplesPerBucket, audio.getNumSamples() - start);
            const auto range = juce::FloatVectorOperations::findMinAndMax(
                audio.getReadPointer(channel, start), length);

            peaks.buckets[static_cast<std::size_t>(channel * peaks.numBuckets + bucket)] = {
                range.getStart(), range.getEnd()};
        }
    }

    return peaks;
}

WaveformPeaks::Range WaveformPeaks::getBucket(int channel, int bucket) const noexcept
{
    if (channel < 0 || channel >= numChannels || bucket < 0 || bucket >= numBuckets)
        return {};

    return buckets[static_cast<std::size_t>(channel * numBuckets + bucket)];
}

WaveformPeaks::Range WaveformPeaks::getRange(int channel, juce::int64 start,
                                             juce::int64 end) const noexcept
{
    start = std::max(juce::int64{0}, start);
    end = std::min(numSamples, end);

    if (channel < 0 || channel >= numChannels || start >= end)
        return {};

    const auto firstBucket = static_cast<int>(start / samplesPerBucket);
    const auto lastBucket = static_cast<int>((end - 1) / samplesPerBucket);

    Range combined = getBucket(channel, firstBucket);
    for (int bucket = firstBucket + 1; bucket <= lastBucket; ++bucket)
    {
        const auto next = getBucket(channel, bucket);
        combined.min = std::min(combined.min, next.min);
        combined.max = std::max(combined.max, next.max);
    }

    return combined;
}

} // namespace daw::engine::dsp
