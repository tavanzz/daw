#include "daw/engine/dsp/WaveformPeaks.h"

#include <algorithm>

namespace daw::engine::dsp
{

WaveformPeaks WaveformPeaks::compute(const juce::AudioBuffer<float>& audio, int samplesPerBucket)
{
    Builder builder(audio.getNumChannels(), samplesPerBucket);
    builder.append(audio.getArrayOfReadPointers(), audio.getNumSamples());
    return builder.build();
}

WaveformPeaks::Builder::Builder(int channels, int bucketSize)
    : numChannels(std::max(0, channels))
    , samplesPerBucket(std::max(1, bucketSize))
    , complete(static_cast<std::size_t>(numChannels))
    , partial(static_cast<std::size_t>(numChannels))
{
    jassert(bucketSize > 0);
}

void WaveformPeaks::Builder::append(const float* const* channels, int count)
{
    int done = 0;
    while (done < count)
    {
        const auto filled = static_cast<int>(numSamples % samplesPerBucket);
        const int length = std::min(count - done, samplesPerBucket - filled);

        for (std::size_t channel = 0; channel < partial.size(); ++channel)
        {
            const auto range =
                juce::FloatVectorOperations::findMinAndMax(channels[channel] + done, length);
            auto& bucket = partial[channel];
            bucket = filled == 0 ? Range{range.getStart(), range.getEnd()}
                                 : Range{std::min(bucket.min, range.getStart()),
                                         std::max(bucket.max, range.getEnd())};

            if (filled + length == samplesPerBucket)
                complete[channel].push_back(bucket);
        }

        numSamples += length;
        done += length;
    }
}

WaveformPeaks WaveformPeaks::Builder::build() const
{
    WaveformPeaks peaks;
    peaks.numChannels = numChannels;
    peaks.numSamples = numSamples;
    peaks.samplesPerBucket = samplesPerBucket;
    peaks.numBuckets = static_cast<int>((numSamples + samplesPerBucket - 1) / samplesPerBucket);
    peaks.buckets.reserve(static_cast<std::size_t>(numChannels) *
                          static_cast<std::size_t>(peaks.numBuckets));

    const bool hasPartial = numSamples % samplesPerBucket != 0;
    for (std::size_t channel = 0; channel < complete.size(); ++channel)
    {
        const auto& full = complete[channel];
        peaks.buckets.insert(peaks.buckets.end(), full.begin(), full.end());
        if (hasPartial)
            peaks.buckets.push_back(partial[channel]);
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
