#include "daw/engine/fx/Equaliser.h"

#include "support/EffectRunner.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <complex>
#include <cstring>
#include <memory>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using daw::engine::fx::Equaliser;
using daw::test::amplitudeOf;
using daw::test::decibels;
using daw::test::EffectRunner;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr double amplitude = 0.25;

// One-third-octave frequencies from 20 Hz to 20 kHz.
std::vector<double> thirdOctaves()
{
    std::vector<double> frequencies;
    for (int i = -17; i <= 13; ++i)
        frequencies.push_back(1000.0 * std::pow(2.0, i / 3.0));
    return frequencies;
}

// The analog prototypes (Audio EQ Cookbook, Butterworth), at the frequency the
// bilinear transform maps f to: what a band should do, worked out independently of
// the equaliser's code.
double prewarped(double frequency, double bandFrequency)
{
    return std::tan(std::numbers::pi * frequency / sampleRate) /
           std::tan(std::numbers::pi * bandFrequency / sampleRate);
}

double bell(double f, double f0, double gainDb, double q)
{
    const std::complex<double> s(0.0, prewarped(f, f0));
    const double a = std::pow(10.0, gainDb / 40.0);
    return std::abs((s * s + s * (a / q) + 1.0) / (s * s + s / (a * q) + 1.0));
}

double lowShelf(double f, double f0, double gainDb, double q)
{
    const std::complex<double> s(0.0, prewarped(f, f0));
    const double a = std::pow(10.0, gainDb / 40.0);
    const double b = std::sqrt(a) / q;
    return std::abs(a * (s * s + b * s + a) / (a * s * s + b * s + 1.0));
}

double highShelf(double f, double f0, double gainDb, double q)
{
    const std::complex<double> s(0.0, prewarped(f, f0));
    const double a = std::pow(10.0, gainDb / 40.0);
    const double b = std::sqrt(a) / q;
    return std::abs(a * (a * s * s + b * s + 1.0) / (s * s + b * s + a));
}

double butterworth(double f, double f0, int order, bool highPass)
{
    const double w = prewarped(f, f0);
    return 1.0 / std::sqrt(1.0 + std::pow(highPass ? 1.0 / w : w, 2.0 * order));
}

// An equaliser with every band neutral (and the passes off), then the changes.
std::unique_ptr<EffectRunner> neutralEqualiser()
{
    auto runner = std::make_unique<EffectRunner>(std::make_unique<Equaliser>(), sampleRate);
    return runner;
}

// The measured gain (dB) at frequency: 0.3 s of sine, the last 0.15 s measured.
double measure(EffectRunner& runner, double frequency)
{
    runner.instance.getEffect().reset();
    const auto input = daw::test::sine(frequency, sampleRate, 14400, amplitude);
    const auto output = runner.run(input);
    return decibels(amplitudeOf(output, 0, frequency, sampleRate, 7200, 7200) / amplitude);
}

} // namespace

TEST_CASE("A neutral equaliser passes the audio bit for bit", "[engine][fx][eq]")
{
    EffectRunner runner(std::make_unique<Equaliser>(), sampleRate);
    juce::AudioBuffer<float> input(2, 20000);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 20000; ++i)
            input.setSample(channel, i, 0.5f * daw::test::testInputSample(channel, i) + 0.001f);

    const int blockSize = GENERATE(1, 64, 512);
    const auto output = runner.run(input, blockSize);

    bool same = true;
    for (int channel = 0; channel < 2; ++channel)
        same = same && std::memcmp(output.getReadPointer(channel), input.getReadPointer(channel),
                                   sizeof(float) * 20000) == 0;
    CHECK(same);
    CHECK(runner.heapOperations == 0);
    CHECK(runner.instance.getLatency() == 0);
}

TEST_CASE("Bells and shelves measure as the Audio EQ Cookbook says", "[engine][fx][eq]")
{
    struct Case
    {
        const char* band;
        double frequency;
        double gain;
        double q;
        double (*theory)(double, double, double, double);
    };
    const auto test = GENERATE(Case{"bell2", 1000.0, 12.0, 2.0, bell},
                               Case{"bell1", 100.0, -18.0, 0.5, bell},
                               Case{"bell4", 9000.0, 6.0, 8.0, bell},
                               Case{"lowShelf", 200.0, 6.0, 0.7071, lowShelf},
                               Case{"highShelf", 5000.0, -9.0, 0.7071, highShelf},
                               Case{"highShelf", 2000.0, 15.0, 2.0, highShelf});

    auto runner = neutralEqualiser();
    const juce::String band(test.band);
    REQUIRE(runner->set(band + "Frequency", static_cast<float>(test.frequency)));
    REQUIRE(runner->set(band + "Gain", static_cast<float>(test.gain)));
    REQUIRE(runner->set(band + "Q", static_cast<float>(test.q)));
    (void) runner->run(daw::test::sine(1000.0, sampleRate, 512, 0.1)); // values in place

    // The values the equaliser really got (they went through 0..1 and back).
    const auto values = runner->values();
    const auto parameters = Equaliser::parametersOf(
        runner->instance.getDescription().indexOf(band + "On") / 5);
    const double f0 = values[static_cast<std::size_t>(parameters.frequency)];
    const double gain = values[static_cast<std::size_t>(parameters.gain)];
    const double q = values[static_cast<std::size_t>(parameters.q)];

    CAPTURE(test.band, f0, gain, q);
    double worst = 0.0;
    for (const double frequency : thirdOctaves())
    {
        const double measured = measure(*runner, frequency);
        const double expected = decibels(test.theory(frequency, f0, gain, q));
        const double drawn = decibels(Equaliser::magnitude(values, frequency, sampleRate));
        worst = std::max({worst, std::abs(measured - expected), std::abs(drawn - expected)});
    }
    CHECK(worst < 0.05);

    // At the band's frequency: the full gain for a bell, half of it for a shelf.
    const bool isBell = band.startsWith("bell");
    CHECK_THAT(measure(*runner, f0), WithinAbs(isBell ? gain : gain / 2.0, 0.02));
    CHECK(runner->heapOperations == 0);
}

TEST_CASE("High- and low-pass are Butterworth: -3 dB at their frequency, 6 dB/oct per order",
          "[engine][fx][eq]")
{
    const int slope = GENERATE(0, 1, 2, 3, 4, 5);
    const bool highPass = GENERATE(true, false);
    const juce::String band = highPass ? "highPass" : "lowPass";
    const double cutoff = highPass ? 80.0 : 8000.0;
    const int order = Equaliser::slopes[static_cast<std::size_t>(slope)] / 6;

    auto runner = neutralEqualiser();
    REQUIRE(runner->set(band + "On", 1.0f));
    REQUIRE(runner->set(band + "Frequency", static_cast<float>(cutoff)));
    REQUIRE(runner->set(band + "Slope", static_cast<float>(slope)));
    (void) runner->run(daw::test::sine(1000.0, sampleRate, 512, 0.1));

    const auto values = runner->values();
    const auto index = static_cast<std::size_t>(Equaliser::parametersOf(highPass ? 0 : 7).frequency);
    const double f0 = values[index];

    CAPTURE(band, order, f0);
    double worst = 0.0;
    for (const double frequency : thirdOctaves())
    {
        const double expected = decibels(butterworth(frequency, f0, order, highPass));
        if (expected < -80.0)
            continue; // below the measurement's noise
        const double measured = measure(*runner, frequency);
        const double drawn = decibels(Equaliser::magnitude(values, frequency, sampleRate));
        worst = std::max({worst, std::abs(measured - expected), std::abs(drawn - expected)});
    }
    CHECK(worst < 0.05);
    CHECK_THAT(measure(*runner, f0), WithinAbs(-3.0103, 0.02));

    CHECK(runner->heapOperations == 0);
}

TEST_CASE("Every band together multiplies their responses", "[engine][fx][eq]")
{
    auto runner = neutralEqualiser();
    runner->set("highPassOn", 1.0f);
    runner->set("highPassFrequency", 40.0f);
    runner->set("highPassSlope", 3.0f);
    runner->set("lowShelfGain", 4.0f);
    runner->set("bell1Gain", -6.0f);
    runner->set("bell2Gain", 3.0f);
    runner->set("bell2Q", 4.0f);
    runner->set("bell3Gain", -12.0f);
    runner->set("bell4Gain", 9.0f);
    runner->set("highShelfGain", -3.0f);
    runner->set("lowPassOn", 1.0f);
    runner->set("lowPassFrequency", 15000.0f);
    runner->set("outputGain", 3.0f);
    (void) runner->run(daw::test::sine(1000.0, sampleRate, 512, 0.1));
    const auto values = runner->values();

    double worst = 0.0;
    for (const double frequency : thirdOctaves())
    {
        double expected = std::pow(10.0, 3.0 / 20.0);
        for (int band = 0; band < Equaliser::numBands; ++band)
            expected *= Equaliser::bandMagnitude(values, band, frequency, sampleRate);
        worst = std::max(worst, std::abs(measure(*runner, frequency) - decibels(expected)));
    }
    CHECK(worst < 0.05);
}

TEST_CASE("Sweeping a band and switching bands while playing stay smooth", "[engine][fx][eq]")
{
    auto runner = neutralEqualiser();
    runner->set("bell3Gain", 12.0f);
    runner->set("bell3Q", 4.0f);

    // The bell sweeps from 20 Hz to 20 kHz in one second, every sample, over noise.
    std::vector<float> sweep(48000);
    for (std::size_t i = 0; i < sweep.size(); ++i)
        sweep[i] = static_cast<float>(i) / 48000.0f;
    REQUIRE(runner->automate("bell3Frequency", sweep));

    juce::AudioBuffer<float> noise(1, 48000);
    for (int i = 0; i < 48000; ++i)
        noise.setSample(0, i, 0.1f * daw::test::testInputSample(0, i));
    const auto swept = runner->run(noise, 256);
    CHECK(swept.getMagnitude(0, 48000) < 0.1f * 4.0f * 2.0f);
    CHECK(swept.getMagnitude(0, 48000) > 0.05f);

    // Switching the high-pass on at 48 dB/oct under a 1 kHz tone: no click, the
    // tone just goes on (the pass is far below it).
    auto switching = neutralEqualiser();
    const auto tone = daw::test::sine(1000.0, sampleRate, 9600, 0.5);
    juce::AudioBuffer<float> first(1, 4800);
    juce::AudioBuffer<float> second(1, 4800);
    first.copyFrom(0, 0, tone, 0, 0, 4800);
    second.copyFrom(0, 0, tone, 0, 4800, 4800);
    const auto before = switching->run(first);
    switching->set("highPassOn", 1.0f);
    switching->set("highPassFrequency", 100.0f);
    switching->set("highPassSlope", 5.0f);
    const auto after = switching->run(second);

    float largest = std::abs(after.getSample(0, 0) - before.getSample(0, 4799));
    for (int i = 1; i < 4800; ++i)
        largest = std::max(largest, std::abs(after.getSample(0, i) - after.getSample(0, i - 1)));
    const double sineStep = 0.5 * 2.0 * std::numbers::pi * 1000.0 / sampleRate;
    CHECK(largest < sineStep * 1.1);
    CHECK(switching->heapOperations == 0);
}
