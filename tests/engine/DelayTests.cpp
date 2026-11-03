#include "daw/engine/fx/Delay.h"

#include "support/EffectRunner.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using daw::engine::fx::Delay;
using daw::test::EffectRunner;

namespace
{

constexpr double sampleRate = 48000.0;

// Wet only, filters off: the echoes are exact copies.
std::unique_ptr<EffectRunner> plainDelay(float feedback)
{
    auto runner = std::make_unique<EffectRunner>(std::make_unique<Delay>(), sampleRate);
    runner->set("mix", 100.0f);
    runner->set("feedback", feedback);
    runner->set("lowCut", 20.0f);
    runner->set("highCut", 20000.0f);
    return runner;
}

juce::AudioBuffer<float> impulse(int length, int at, int channel = -1)
{
    juce::AudioBuffer<float> buffer(2, length);
    buffer.clear();
    for (int c = 0; c < 2; ++c)
        if (channel < 0 || channel == c)
            buffer.setSample(c, at, 1.0f);
    return buffer;
}

float valueOf(EffectRunner& runner, const char* id)
{
    return runner.values()[static_cast<std::size_t>(runner.instance.getDescription().indexOf(id))];
}

// Where the output is not zero.
std::vector<int> nonZero(const juce::AudioBuffer<float>& buffer, int channel)
{
    std::vector<int> positions;
    for (int i = 0; i < buffer.getNumSamples(); ++i)
        if (buffer.getSample(channel, i) != 0.0f)
            positions.push_back(i);
    return positions;
}

} // namespace

TEST_CASE("Echoes come exactly the delay time later, each the feedback quieter",
          "[engine][fx][delay]")
{
    const int blockSize = GENERATE(1, 100, 512);
    auto runner = plainDelay(50.0f);
    runner->set("time", 250.0f);

    const auto output = runner->run(impulse(60000, 100), blockSize);
    const float amount = std::min(valueOf(*runner, "feedback"), 95.0f) / 100.0f;

    // 250 ms is 12000 samples: echoes at 12100, 24100, 36100, 48100.
    CAPTURE(blockSize);
    CHECK(nonZero(output, 0) == std::vector<int>{12100, 24100, 36100, 48100});
    float expected = 1.0f;
    for (const int position : {12100, 24100, 36100, 48100})
    {
        CHECK(output.getSample(0, position) == expected);
        CHECK(output.getSample(1, position) == expected);
        expected *= amount;
    }
    CHECK(runner->heapOperations == 0);
    CHECK(runner->instance.getLatency() == 0);
}

TEST_CASE("Synced echoes are the note's length at the tempo, to the sample", "[engine][fx][delay]")
{
    struct Case
    {
        double tempo;
        const char* note;
        int samples;
    };
    const auto test = GENERATE(Case{120.0, "1/4", 24000}, Case{100.0, "1/8D", 21600},
                               Case{133.0, "1/16T", 3609}, Case{90.0, "1/2T", 42667},
                               Case{60.0, "1/1", 192000}, Case{30.0, "1/1", 192000});

    int noteIndex = -1;
    for (std::size_t i = 0; i < Delay::noteValues.size(); ++i)
        if (juce::String(Delay::noteValues[i].name) == test.note)
            noteIndex = static_cast<int>(i);
    REQUIRE(noteIndex >= 0);

    auto runner = plainDelay(0.0f);
    runner->set("sync", 1.0f);
    runner->set("note", static_cast<float>(noteIndex));
    runner->time.tempo = test.tempo;
    const auto output = runner->run(impulse(test.samples + 1000, 10));

    CAPTURE(test.tempo, test.note);
    CHECK(Delay::delaySamples(true, 0.0, noteIndex, test.tempo, sampleRate) == test.samples);
    CHECK(nonZero(output, 0) == std::vector<int>{10 + test.samples});
}

TEST_CASE("Feedback stays below 100 % and echoes always die away", "[engine][fx][delay]")
{
    // Asking for 100 % gives 95 %.
    auto runner = plainDelay(100.0f);
    runner->set("time", 100.0f);
    CHECK(runner->set("feedback", 100.0f));
    const auto output = runner->run(impulse(12 * 48000, 0));
    CHECK(valueOf(*runner, "feedback") == 95.0f);

    // Echo k (every 4800 samples) is 0.95^(k-1): falling, and 100 dB down in time.
    float previous = 2.0f;
    bool falling = true;
    for (int k = 1; k < 110; ++k)
    {
        const float echo = output.getSample(0, 4800 * k);
        falling = falling && echo < previous && echo > 0.0f;
        previous = echo;
    }
    CHECK(falling);
    CHECK(output.getMagnitude(0, 10 * 48000, 2 * 48000) < 0.01f);

    // Full-scale noise into 95 % feedback, with the filters on: bounded, finite.
    EffectRunner noisy(std::make_unique<Delay>(), sampleRate);
    noisy.set("feedback", 95.0f);
    noisy.set("mix", 100.0f);
    noisy.set("time", 37.0f);
    juce::AudioBuffer<float> noise(2, 4 * 48000);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample(channel, i, daw::test::testInputSample(channel, i));
    const auto loud = noisy.run(noise);
    CHECK(loud.getMagnitude(0, loud.getNumSamples()) < 20.0f);
    bool finite = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < loud.getNumSamples(); ++i)
            finite = finite && std::isfinite(loud.getSample(channel, i));
    CHECK(finite);
    CHECK(noisy.heapOperations == 0);

    std::vector<float> values(8, 0.0f);
    values[Delay::time] = 100.0f;
    values[Delay::feedback] = 50.0f;
    CHECK_THAT(Delay().getTailSeconds(values), WithinRel(0.1 * (1.0 + std::log(1e-5) / std::log(0.5)), 1e-3));
}

TEST_CASE("Ping-pong echoes cross from side to side", "[engine][fx][delay]")
{
    auto runner = plainDelay(50.0f);
    runner->set("time", 10.0f); // 480 samples
    runner->set("pingPong", 1.0f);
    const auto output = runner->run(impulse(2000, 0, 0));
    const float amount = valueOf(*runner, "feedback") / 100.0f;

    // The input goes in as mono (half of left + right); echoes alternate.
    CHECK(nonZero(output, 0) == std::vector<int>{480, 1440});
    CHECK(nonZero(output, 1) == std::vector<int>{960, 1920});
    CHECK(output.getSample(0, 480) == 0.5f);
    CHECK(output.getSample(1, 960) == amount * 0.5f);
}

TEST_CASE("A new delay time crossfades between the taps instead of jumping", "[engine][fx][delay]")
{
    auto runner = plainDelay(0.0f);
    runner->set("time", 100.0f);
    const auto tone = daw::test::sine(440.0, sampleRate, 48000, 0.5);
    juce::AudioBuffer<float> first(1, 24000);
    juce::AudioBuffer<float> second(1, 24000);
    first.copyFrom(0, 0, tone, 0, 0, 24000);
    second.copyFrom(0, 0, tone, 0, 24000, 24000);

    const auto before = runner->run(first);
    runner->set("time", 113.0f);
    const auto after = runner->run(second);

    float largest = std::abs(after.getSample(0, 0) - before.getSample(0, 23999));
    for (int i = 1; i < 24000; ++i)
        largest = std::max(largest, std::abs(after.getSample(0, i) - after.getSample(0, i - 1)));

    // A sine's own largest step, plus what moving between two of them over 50 ms adds.
    const double sineStep = 0.5 * 2.0 * std::numbers::pi * 440.0 / sampleRate;
    CHECK(largest < sineStep + 2.0 * 0.5 / 2400.0 + 1e-4);

    // Afterwards: the tone delayed by the new time, exactly.
    const int delay = Delay::delaySamples(false, valueOf(*runner, "time"), 0, 120.0, sampleRate);
    CHECK(delay == 5424);
    CHECK(after.getSample(0, 20000) == second.getSample(0, 20000 - delay));
}
