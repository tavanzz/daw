#include "daw/engine/fx/Compressor.h"
#include "daw/engine/fx/Limiter.h"

#include "support/EffectRunner.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <cstring>
#include <memory>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using daw::engine::fx::Compressor;
using daw::engine::fx::Limiter;
using daw::test::EffectRunner;

namespace
{

constexpr double sampleRate = 48000.0;

juce::AudioBuffer<float> constant(int length, double decibels)
{
    juce::AudioBuffer<float> buffer(1, length);
    const auto value = static_cast<float>(std::pow(10.0, decibels / 20.0));
    juce::FloatVectorOperations::fill(buffer.getWritePointer(0), value, length);
    return buffer;
}

// The gain reduction (dB) of output sample i, for an input of steady level dB.
double reductionAt(const juce::AudioBuffer<float>& input, const juce::AudioBuffer<float>& output,
                   int i)
{
    return -20.0 * std::log10(static_cast<double>(output.getSample(0, i)) /
                              static_cast<double>(input.getSample(0, i)));
}

// The compressor's static curve as Giannoulis, Massberg and Reiss give it.
double giannoulis(double x, double threshold, double ratio, double knee)
{
    if (2.0 * (x - threshold) < -knee)
        return x;
    if (knee > 0.0 && 2.0 * std::abs(x - threshold) <= knee)
        return x + (1.0 / ratio - 1.0) * std::pow(x - threshold + knee / 2.0, 2.0) / (2.0 * knee);
    return threshold + (x - threshold) / ratio;
}

std::unique_ptr<EffectRunner> compressor(double threshold, double ratio, double knee,
                                         double attack, double release)
{
    auto runner = std::make_unique<EffectRunner>(std::make_unique<Compressor>(), sampleRate);
    runner->set("threshold", static_cast<float>(threshold));
    runner->set("ratio", static_cast<float>(ratio));
    runner->set("knee", static_cast<float>(knee));
    runner->set("attack", static_cast<float>(attack));
    runner->set("release", static_cast<float>(release));
    return runner;
}

// The values the effect really works with (after the trip through 0..1).
float valueOf(EffectRunner& runner, const char* id)
{
    (void) runner.run(constant(1, -100.0));
    return runner.values()[static_cast<std::size_t>(runner.instance.getDescription().indexOf(id))];
}

Limiter& limiterOf(EffectRunner& runner)
{
    return dynamic_cast<Limiter&>(runner.instance.getEffect());
}

} // namespace

TEST_CASE("The compressor's static curve has its threshold, ratio and knee", "[engine][fx][dynamics]")
{
    struct Setting
    {
        double threshold, ratio, knee;
    };
    const auto setting = GENERATE(Setting{-20.0, 4.0, 0.0}, Setting{-30.0, 2.0, 12.0},
                                  Setting{-10.0, 20.0, 6.0}, Setting{-40.0, 1.5, 24.0});

    auto runner = compressor(setting.threshold, setting.ratio, setting.knee, 0.1, 10.0);
    const double threshold = valueOf(*runner, "threshold");
    const double ratio = valueOf(*runner, "ratio");
    const double knee = valueOf(*runner, "knee");

    CAPTURE(threshold, ratio, knee);
    double worst = 0.0;
    for (double level = -60.0; level <= 0.0; level += 1.5)
    {
        runner->instance.getEffect().reset();
        const auto input = constant(4000, level);
        const auto output = runner->run(input);
        const double outputLevel = level - reductionAt(input, output, 3999);
        worst = std::max(worst, std::abs(outputLevel - giannoulis(level, threshold, ratio, knee)));
        worst = std::max(worst, std::abs(outputLevel -
                                         Compressor::staticCurve(level, threshold, ratio, knee)));
    }
    CHECK(worst < 0.01);
    CHECK(runner->heapOperations == 0);
    CHECK(runner->instance.getLatency() == 0);
}

TEST_CASE("Compressor attack and release are time constants", "[engine][fx][dynamics]")
{
    const double attack = GENERATE(1.0, 10.0, 50.0);
    const double release = GENERATE(30.0, 300.0);

    auto runner = compressor(-30.0, 4.0, 0.0, attack, release);
    const double attackMs = valueOf(*runner, "attack");
    const double releaseMs = valueOf(*runner, "release");
    runner->instance.getEffect().reset();

    // -40 dB (no reduction), then -10 dB: 20 dB over at 4:1 wants 15 dB; then -40 dB
    // again, wanting none.
    const int stepUp = 1000;
    const int stepDown = stepUp + 96000;
    juce::AudioBuffer<float> input(1, stepDown + 96000);
    input.copyFrom(0, 0, constant(stepUp, -40.0), 0, 0, stepUp);
    input.copyFrom(0, stepUp, constant(96000, -10.0), 0, 0, 96000);
    input.copyFrom(0, stepDown, constant(96000, -40.0), 0, 0, 96000);
    const auto output = runner->run(input);

    CAPTURE(attackMs, releaseMs);
    CHECK(reductionAt(input, output, stepUp - 1) == 0.0);

    // After one attack time, 1 - 1/e of the 15 dB; after one release time, 1/e is left.
    const auto attackSamples = static_cast<int>(std::lround(attackMs * 0.001 * sampleRate));
    const auto releaseSamples = static_cast<int>(std::lround(releaseMs * 0.001 * sampleRate));
    const double alphaA = std::exp(-1.0 / (attackMs * 0.001 * sampleRate));
    const double alphaR = std::exp(-1.0 / (releaseMs * 0.001 * sampleRate));
    CHECK_THAT(reductionAt(input, output, stepUp + attackSamples - 1),
               WithinAbs(15.0 * (1.0 - std::pow(alphaA, attackSamples)), 1e-3));
    CHECK_THAT(reductionAt(input, output, stepUp + attackSamples - 1),
               WithinRel(15.0 * (1.0 - std::exp(-1.0)), 0.01));
    CHECK_THAT(reductionAt(input, output, stepDown - 1), WithinAbs(15.0, 1e-3));

    const double atRelease = reductionAt(input, output, stepDown + releaseSamples - 1);
    CHECK_THAT(atRelease, WithinRel(15.0 * std::exp(-1.0), 0.01));
    CHECK_THAT(atRelease, WithinAbs(15.0 * std::pow(alphaR, releaseSamples), 1e-3));
}

TEST_CASE("Compressor makeup, mix and meter", "[engine][fx][dynamics]")
{
    auto runner = compressor(-20.0, 4.0, 0.0, 0.1, 10.0);
    runner->set("makeup", 6.0f);
    runner->set("mix", 50.0f);
    const auto input = constant(4000, -10.0);
    const auto output = runner->run(input);

    // 10 dB over at 4:1: 7.5 dB less, +6 makeup: -1.5 dB wet, mixed half and half.
    const double makeup = valueOf(*runner, "makeup");
    const double wet = std::pow(10.0, (makeup - 7.5) / 20.0);
    CHECK_THAT(output.getSample(0, 3999) / input.getSample(0, 3999), WithinRel(0.5 * wet + 0.5, 1e-4));

    auto& meter = dynamic_cast<Compressor&>(runner->instance.getEffect()).getGainReduction();
    CHECK_THAT(meter.take(), WithinAbs(7.5, 0.01));
    CHECK(meter.take() == 0.0f);
}

TEST_CASE("The limiter's output never goes above its ceiling", "[engine][fx][dynamics]")
{
    const float ceilingDb = GENERATE(-0.3f, -6.0f, -24.0f);
    const int signal = GENERATE(0, 1, 2, 3, 4, 5);

    juce::AudioBuffer<float> input(2, 48000);
    for (int i = 0; i < 48000; ++i)
    {
        float left = 0.0f;
        float right = 0.0f;
        switch (signal)
        {
        case 0: // full-scale noise
            left = daw::test::testInputSample(0, i);
            right = daw::test::testInputSample(1, i);
            break;
        case 1: // lone impulses
            left = i % 997 == 0 ? 1.0f : 0.0f;
            right = i % 1009 == 0 ? -1.0f : 0.0f;
            break;
        case 2: // a square wave
            left = right = (i / 240) % 2 == 0 ? 1.0f : -1.0f;
            break;
        case 3: // a sweep, 20 Hz to 20 kHz
        {
            const double t = i / sampleRate;
            const double phase = 2.0 * std::numbers::pi * 20.0 * (std::pow(1000.0, t) - 1.0) / std::log(1000.0);
            left = right = static_cast<float>(std::sin(phase));
            break;
        }
        case 4: // the worst flip, every sample
            left = i % 2 == 0 ? 1.0f : -1.0f;
            right = -left;
            break;
        default: // bursts of noise between silence
            left = (i / 3000) % 2 == 0 ? daw::test::testInputSample(2, i) : 0.0f;
            right = left * 0.5f;
            break;
        }
        input.setSample(0, i, left);
        input.setSample(1, i, right);
    }

    EffectRunner runner(std::make_unique<Limiter>(), sampleRate);
    runner.set("ceiling", ceilingDb);
    runner.set("inputGain", 18.0f);
    runner.set("release", 50.0f);
    const auto output = runner.run(input, 331);

    const auto ceiling = static_cast<float>(std::pow(10.0, valueOf(runner, "ceiling") / 20.0));
    CAPTURE(ceilingDb, signal);
    CHECK(output.getMagnitude(0, 48000) <= ceiling);
    CHECK(output.getMagnitude(0, 48000) > ceiling * 0.5f);
    CHECK(limiterOf(runner).getLargestExcess() < 1e-6); // the final clip only rounds
    CHECK(runner.heapOperations == 0);
}

TEST_CASE("The limiter's latency is its lookahead, and below the ceiling it changes nothing",
          "[engine][fx][dynamics]")
{
    EffectRunner runner(std::make_unique<Limiter>(), sampleRate);
    CHECK(runner.instance.getLatency() == 72); // 1.5 ms at 48 kHz

    juce::AudioBuffer<float> input(2, 20000);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 20000; ++i)
            input.setSample(channel, i, 0.25f * daw::test::testInputSample(channel, i));
    const auto output = runner.run(input, 100);

    bool delayedExactly = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 20000; ++i)
            delayedExactly = delayedExactly &&
                             output.getSample(channel, i) == (i < 72 ? 0.0f : input.getSample(channel, i - 72));
    CHECK(delayedExactly);

    EffectRunner at44(std::make_unique<Limiter>(), 44100.0);
    CHECK(at44.instance.getLatency() == 66);
}

TEST_CASE("The limiter comes back at its release time", "[engine][fx][dynamics]")
{
    const double release = GENERATE(20.0, 200.0);
    EffectRunner runner(std::make_unique<Limiter>(), sampleRate);
    runner.set("ceiling", -6.0f);
    runner.set("release", static_cast<float>(release));
    const double releaseMs = valueOf(runner, "release");
    runner.instance.getEffect().reset();

    // A steady 0.1 with one spike of 2.0 at 1000: the gain dips for the spike and
    // recovers exponentially; 0.1 shows the gain on the samples around it.
    juce::AudioBuffer<float> input(1, 96000);
    juce::FloatVectorOperations::fill(input.getWritePointer(0), 0.1f, 96000);
    input.setSample(0, 1000, 2.0f);
    const auto output = runner.run(input);

    const auto gainAt = [&output](int i) { return output.getSample(0, i) / 0.1; };
    const int first = 1000 + 72 * 3;
    const auto releaseSamples = static_cast<int>(std::lround(releaseMs * 0.001 * sampleRate));
    const double ratio = (1.0 - gainAt(first + releaseSamples)) / (1.0 - gainAt(first));
    CAPTURE(releaseMs);
    CHECK(gainAt(first) < 0.99);
    CHECK_THAT(ratio, WithinRel(std::exp(-1.0), 1e-3));
    CHECK(limiterOf(runner).getGainReduction().take() > 5.0f);
}
