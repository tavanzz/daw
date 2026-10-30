#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <numbers>

namespace daw::test
{

/** amplitude * sin(2 pi frequency t + phase) sampled at sampleRate, on every channel. */
inline juce::AudioBuffer<float> makeSine(int numChannels, int numSamples, double frequency,
                                         double sampleRate, double amplitude, double phase = 0.0)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);

    for (int n = 0; n < numSamples; ++n)
    {
        const double t = n / sampleRate;
        const auto value = static_cast<float>(
            amplitude * std::sin(2.0 * std::numbers::pi * frequency * t + phase));

        for (int channel = 0; channel < numChannels; ++channel)
            buffer.setSample(channel, n, value);
    }

    return buffer;
}

/** Root mean square of samples [start, start + length) of one channel. */
inline double rms(const juce::AudioBuffer<float>& buffer, int channel, int start, int length)
{
    double sumOfSquares = 0.0;
    for (int n = start; n < start + length; ++n)
    {
        const double value = buffer.getSample(channel, n);
        sumOfSquares += value * value;
    }

    return std::sqrt(sumOfSquares / length);
}

/** RMS of the difference between two channels over [start, start + length). */
inline double rmsDifference(const juce::AudioBuffer<float>& a, int channelA,
                            const juce::AudioBuffer<float>& b, int channelB, int start, int length)
{
    double sumOfSquares = 0.0;
    for (int n = start; n < start + length; ++n)
    {
        const double difference =
            static_cast<double>(a.getSample(channelA, n)) - b.getSample(channelB, n);
        sumOfSquares += difference * difference;
    }

    return std::sqrt(sumOfSquares / length);
}

/** Largest absolute sample difference between two buffers of the same size. */
inline float maxAbsDifference(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b)
{
    float largest = 0.0f;
    for (int channel = 0; channel < a.getNumChannels(); ++channel)
        for (int n = 0; n < a.getNumSamples(); ++n)
            largest = std::max(largest, std::abs(a.getSample(channel, n) - b.getSample(channel, n)));

    return largest;
}

inline double toDecibels(double ratio)
{
    return 20.0 * std::log10(ratio);
}

} // namespace daw::test
