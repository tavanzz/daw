#include "daw/engine/dsp/Resampler.h"

#include "support/SignalAnalysis.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>

using daw::engine::dsp::resample;
using daw::engine::dsp::resampledLength;
using daw::test::makeSine;
using daw::test::toDecibels;

namespace
{
// Skip the start and end, where the filter sees silence beyond the input.
constexpr int edgeMargin = 2000;

struct Conversion
{
    double inputRate;
    double outputRate;
};
} // namespace

TEST_CASE("Converting to the same sample rate returns an exact copy", "[engine][resampler]")
{
    const auto input = makeSine(2, 1000, 440.0, 48000.0, 0.7);
    const auto output = resample(input, 48000.0, 48000.0);

    REQUIRE(output.getNumChannels() == 2);
    REQUIRE(output.getNumSamples() == 1000);
    CHECK(daw::test::maxAbsDifference(input, output) == 0.0f);
}

TEST_CASE("Output length is the input length scaled by the rate ratio", "[engine][resampler]")
{
    CHECK(resampledLength(44100, 44100.0, 48000.0) == 48000);
    CHECK(resampledLength(48000, 48000.0, 44100.0) == 44100);
    CHECK(resampledLength(96000, 96000.0, 48000.0) == 48000);
    CHECK(resampledLength(1, 44100.0, 48000.0) == 1);
    CHECK(resampledLength(1000, 22050.0, 48000.0) == 2177); // 2176.87 rounds up
    CHECK(resampledLength(0, 44100.0, 48000.0) == 0);

    const auto input = makeSine(1, 4410, 1000.0, 44100.0, 0.5);
    CHECK(resample(input, 44100.0, 48000.0).getNumSamples() == 4800);
}

TEST_CASE("Tones in the passband come through unchanged", "[engine][resampler]")
{
    const auto conversion = GENERATE(Conversion{44100.0, 48000.0}, Conversion{48000.0, 44100.0},
                                     Conversion{44100.0, 96000.0}, Conversion{96000.0, 48000.0},
                                     Conversion{22050.0, 48000.0});
    // Up to 45 % of the lower rate: 19.8 kHz at 44.1 kHz.
    const double lowerRate = std::min(conversion.inputRate, conversion.outputRate);
    const double frequency = GENERATE_COPY(100.0, 1000.0, 0.25 * lowerRate, 0.45 * lowerRate);

    const int inputLength = static_cast<int>(conversion.inputRate / 2);
    const auto input = makeSine(1, inputLength, frequency, conversion.inputRate, 0.5);
    const auto output = resample(input, conversion.inputRate, conversion.outputRate);
    const auto expected =
        makeSine(1, output.getNumSamples(), frequency, conversion.outputRate, 0.5);

    const int length = output.getNumSamples() - 2 * edgeMargin;
    const double errorDb =
        toDecibels(daw::test::rmsDifference(output, 0, expected, 0, edgeMargin, length) /
                   daw::test::rms(expected, 0, edgeMargin, length));

    CAPTURE(conversion.inputRate, conversion.outputRate, frequency, errorDb);
    // The error contains passband ripple, images and aliases together.
    CHECK(errorDb < -90.0);
}

TEST_CASE("Tones above the lower Nyquist frequency are removed", "[engine][resampler]")
{
    const auto conversion =
        GENERATE(Conversion{96000.0, 48000.0}, Conversion{48000.0, 44100.0},
                 Conversion{192000.0, 44100.0});
    const double outputNyquist = conversion.outputRate / 2;
    const double inputNyquist = conversion.inputRate / 2;
    // Just inside the stopband, and halfway to the input's own Nyquist frequency
    // (above that a tone cannot exist in the input at all).
    const double frequency =
        GENERATE_COPY(outputNyquist * 1.01, (outputNyquist + inputNyquist) / 2);

    const int inputLength = static_cast<int>(conversion.inputRate / 2);
    const auto input = makeSine(1, inputLength, frequency, conversion.inputRate, 1.0);
    const auto output = resample(input, conversion.inputRate, conversion.outputRate);

    const int length = output.getNumSamples() - 2 * edgeMargin;
    const double residueDb = toDecibels(daw::test::rms(output, 0, edgeMargin, length) /
                                        (1.0 / std::sqrt(2.0)));

    CAPTURE(conversion.inputRate, conversion.outputRate, frequency, residueDb);
    CHECK(residueDb < -96.0);
}

TEST_CASE("A constant signal keeps its level", "[engine][resampler]")
{
    const auto conversion = GENERATE(Conversion{44100.0, 48000.0}, Conversion{96000.0, 44100.0});

    juce::AudioBuffer<float> input(1, static_cast<int>(conversion.inputRate / 4));
    input.clear();
    juce::FloatVectorOperations::fill(input.getWritePointer(0), 0.25f, input.getNumSamples());

    const auto output = resample(input, conversion.inputRate, conversion.outputRate);

    float largestError = 0.0f;
    for (int n = edgeMargin; n < output.getNumSamples() - edgeMargin; ++n)
        largestError = std::max(largestError, std::abs(output.getSample(0, n) - 0.25f));

    CAPTURE(conversion.inputRate, conversion.outputRate, largestError);
    CHECK(largestError < 1.0e-5f);
}

TEST_CASE("Each channel is converted independently", "[engine][resampler]")
{
    const int length = 24000;
    const auto left = makeSine(1, length, 440.0, 48000.0, 0.5);
    const auto right = makeSine(1, length, 3000.0, 48000.0, 0.25);

    juce::AudioBuffer<float> stereo(2, length);
    stereo.copyFrom(0, 0, left, 0, 0, length);
    stereo.copyFrom(1, 0, right, 0, 0, length);

    const auto converted = resample(stereo, 48000.0, 44100.0);
    const auto convertedLeft = resample(left, 48000.0, 44100.0);
    const auto convertedRight = resample(right, 48000.0, 44100.0);

    REQUIRE(converted.getNumChannels() == 2);
    const int outputLength = converted.getNumSamples();
    CHECK(daw::test::rmsDifference(converted, 0, convertedLeft, 0, 0, outputLength) == 0.0);
    CHECK(daw::test::rmsDifference(converted, 1, convertedRight, 0, 0, outputLength) == 0.0);
}

TEST_CASE("Empty input gives empty output", "[engine][resampler]")
{
    juce::AudioBuffer<float> input(2, 0);
    const auto output = resample(input, 44100.0, 48000.0);

    CHECK(output.getNumChannels() == 2);
    CHECK(output.getNumSamples() == 0);
}
