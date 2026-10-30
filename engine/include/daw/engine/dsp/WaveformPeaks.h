#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace daw::engine::dsp
{

/**
    Min/max summary of audio for drawing waveforms.

    For every bucket of samplesPerBucket consecutive samples (the last bucket may
    be shorter) it keeps the lowest and highest sample of each channel, so no peak
    is lost at any zoom level. Computed once per file, off the audio thread, and
    never modified afterwards: any thread may read it.
*/
class WaveformPeaks
{
public:
    static constexpr int defaultSamplesPerBucket = 256;

    struct Range
    {
        float min = 0.0f;
        float max = 0.0f;
    };

    WaveformPeaks() = default;

    /** Not real-time safe: allocates and reads the whole buffer. */
    [[nodiscard]] static WaveformPeaks compute(const juce::AudioBuffer<float>& audio,
                                               int samplesPerBucket = defaultSamplesPerBucket);

    [[nodiscard]] int getNumChannels() const noexcept { return numChannels; }
    [[nodiscard]] juce::int64 getNumSamples() const noexcept { return numSamples; }
    [[nodiscard]] int getSamplesPerBucket() const noexcept { return samplesPerBucket; }
    [[nodiscard]] int getNumBuckets() const noexcept { return numBuckets; }

    [[nodiscard]] Range getBucket(int channel, int bucket) const noexcept;

    /** Lowest and highest sample of the channel over samples [start, end), from the
        buckets that overlap it: the result may include up to one bucket's worth of
        samples on either side. Empty or out-of-range spans give {0, 0}. */
    [[nodiscard]] Range getRange(int channel, juce::int64 start, juce::int64 end) const noexcept;

private:
    int numChannels = 0;
    juce::int64 numSamples = 0;
    int samplesPerBucket = defaultSamplesPerBucket;
    int numBuckets = 0;
    std::vector<Range> buckets; // channel after channel
};

} // namespace daw::engine::dsp
