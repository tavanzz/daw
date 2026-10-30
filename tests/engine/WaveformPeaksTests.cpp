#include "daw/engine/dsp/WaveformPeaks.h"

#include "support/SignalAnalysis.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using daw::engine::dsp::WaveformPeaks;

TEST_CASE("Waveform peaks hold the min and max of every bucket", "[engine][waveform]")
{
    // A ramp from 0 to 0.999 on the left, its negation on the right.
    juce::AudioBuffer<float> audio(2, 1000);
    for (int n = 0; n < 1000; ++n)
    {
        audio.setSample(0, n, static_cast<float>(n) / 1000.0f);
        audio.setSample(1, n, -static_cast<float>(n) / 1000.0f);
    }

    const auto peaks = WaveformPeaks::compute(audio, 100);

    CHECK(peaks.getNumChannels() == 2);
    CHECK(peaks.getNumSamples() == 1000);
    CHECK(peaks.getNumBuckets() == 10);

    const auto third = peaks.getBucket(0, 3);
    CHECK(third.min == 0.3f);
    CHECK(third.max == 0.399f);

    const auto mirrored = peaks.getBucket(1, 3);
    CHECK(mirrored.min == -0.399f);
    CHECK(mirrored.max == -0.3f);
}

TEST_CASE("The last bucket covers only the remaining samples", "[engine][waveform]")
{
    juce::AudioBuffer<float> audio(1, 250);
    audio.clear();
    audio.setSample(0, 249, 0.75f);

    const auto peaks = WaveformPeaks::compute(audio, 100);

    CHECK(peaks.getNumBuckets() == 3);
    CHECK(peaks.getBucket(0, 2).max == 0.75f);
    CHECK(peaks.getBucket(0, 1).max == 0.0f);
}

TEST_CASE("A single-sample spike shows at every zoom level", "[engine][waveform]")
{
    juce::AudioBuffer<float> audio(1, 480000);
    audio.clear();
    audio.setSample(0, 123457, -0.9f);

    const auto peaks = WaveformPeaks::compute(audio);

    // Draw the whole file into widths from 100 to 4000 pixel columns.
    for (const int columns : {100, 777, 1920, 4000})
    {
        float lowest = 0.0f;
        for (int x = 0; x < columns; ++x)
        {
            const auto start = static_cast<juce::int64>(x) * 480000 / columns;
            const auto end = static_cast<juce::int64>(x + 1) * 480000 / columns;
            lowest = std::min(lowest, peaks.getRange(0, start, end).min);
        }

        CAPTURE(columns);
        CHECK(lowest == -0.9f);
    }
}

TEST_CASE("Ranges combine the buckets they overlap", "[engine][waveform]")
{
    const auto audio = daw::test::makeSine(1, 48000, 10.0, 48000.0, 0.5);
    const auto peaks = WaveformPeaks::compute(audio);

    const auto whole = peaks.getRange(0, 0, 48000);
    CHECK(whole.max == audio.getSample(0, 1200)); // crest of the first cycle
    CHECK(whole.min == audio.getSample(0, 3600)); // trough

    CHECK(peaks.getRange(0, 100, 100).max == 0.0f); // empty span
    CHECK(peaks.getRange(0, 50000, 60000).max == 0.0f); // past the end
    CHECK(peaks.getRange(3, 0, 48000).max == 0.0f); // no such channel
}

TEST_CASE("Empty audio gives empty peaks", "[engine][waveform]")
{
    const auto peaks = WaveformPeaks::compute(juce::AudioBuffer<float>(2, 0));

    CHECK(peaks.getNumBuckets() == 0);
    CHECK(peaks.getRange(0, 0, 100).max == 0.0f);
}
