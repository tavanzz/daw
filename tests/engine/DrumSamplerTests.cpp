#include "daw/engine/Sample.h"
#include "daw/engine/fx/DrumSampler.h"

#include "support/InstrumentRunner.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <bit>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

namespace midi = daw::engine::midi;
using Catch::Matchers::WithinRel;
using daw::engine::Sample;
using daw::engine::fx::DrumSampler;
using daw::test::InstrumentRunner;

namespace
{

constexpr double rate = 48000.0;

// A hit's level samples after it started: a 0.5 ms attack, then the 10 s decay
// (60 dB in its time).
double decayed(double value, int samplesSinceHit)
{
    constexpr int attack = 24;
    return value * std::pow(10.0, -3.0 * (samplesSinceHit - attack) / (10.0 * rate));
}

std::shared_ptr<const Sample> constantSample(float value, int length)
{
    juce::AudioBuffer<float> audio(1, length);
    audio.clear();
    for (int i = 0; i < length; ++i)
        audio.setSample(0, i, value);
    return Sample::fromAudio(std::move(audio), rate);
}

// A kit whose pad i plays a constant (i + 1) / 64 for a second.
struct Kit
{
    Kit()
    {
        for (int pad = 0; pad < DrumSampler::numPads; ++pad)
            samples.push_back(constantSample(static_cast<float>(pad + 1) / 64.0f, 48000));
        runner = std::make_unique<InstrumentRunner>(std::make_unique<DrumSampler>(), rate);
        for (const auto& sample : samples)
            runner->samples.push_back(sample.get());
        runner->set("velocity", 0.0f);
        for (int pad = 0; pad < DrumSampler::numPads; ++pad)
            runner->set("pad" + juce::String(pad + 1) + "Decay", 10000.0f);
    }

    [[nodiscard]] DrumSampler& instrument() const { return dynamic_cast<DrumSampler&>(*runner->effect); }

    std::vector<std::shared_ptr<const Sample>> samples;
    std::unique_ptr<InstrumentRunner> runner;
};

} // namespace

TEST_CASE("Each pad's key plays its own sample, to its end whatever the note-off",
          "[engine][drums]")
{
    Kit kit;
    for (int pad = 0; pad < DrumSampler::numPads; ++pad)
    {
        const int key = DrumSampler::firstKey + pad;
        const auto out =
            kit.runner->run({midi::noteOn(kit.runner->position, key, 1.0f),
                             midi::noteOff(kit.runner->position + 10, key)},
                            2000);
        CAPTURE(pad);
        CHECK_THAT(out[1000], WithinRel(decayed((pad + 1) / 64.0, 1000), 1e-4));
        kit.runner->run({}, 48000); // it ends with its sample
        CHECK(kit.instrument().getActiveVoices() == 0);
    }

    // Keys no pad is on play nothing.
    const auto out = kit.runner->run({midi::noteOn(kit.runner->position, 60, 1.0f)}, 1000);
    CHECK(out[500] == 0.0f);
    CHECK(kit.instrument().getActiveVoices() == 0);
    CHECK(kit.runner->heapOperations == 0);
}

TEST_CASE("A closed hat chokes the open one; a pad hit again replaces its last hit",
          "[engine][drums]")
{
    Kit kit;
    const double openHat = 11.0 / 64.0;  // pad 11, key 46
    const double closedHat = 7.0 / 64.0; // pad 7, key 42
    auto out = kit.runner->run({midi::noteOn(0, 46, 1.0f), midi::noteOn(4800, 42, 1.0f)}, 9600);
    CHECK_THAT(out[4700], WithinRel(decayed(openHat, 4700), 1e-4));
    // Within 5 ms the open hat is gone: only the closed one is left.
    CHECK_THAT(out[4800 + 300], WithinRel(decayed(closedHat, 300), 1e-4));
    CHECK(kit.instrument().getActiveVoices() == 1);

    // The kick hit again: the first hit fades out over 3 ms, then only the new one is
    // heard.
    Kit kicks;
    out = kicks.runner->run({midi::noteOn(0, 36, 1.0f), midi::noteOn(1000, 36, 1.0f)}, 3000);
    CHECK_THAT(out[1000 + 300], WithinRel(decayed(1.0 / 64.0, 300), 1e-4));
    CHECK(kicks.instrument().getActiveVoices() == 1);
}

TEST_CASE("Pads move to other keys, and their tuning, level and pan apply", "[engine][drums]")
{
    // A 441 Hz tone on pad 1, tuned an octave up and moved to key 60.
    const auto length = 48000;
    juce::AudioBuffer<float> tone(1, length);
    for (int i = 0; i < length; ++i)
        tone.setSample(0, i, static_cast<float>(std::sin(2.0 * std::numbers::pi * 441.0 * i / rate)));
    const auto sample = Sample::fromAudio(std::move(tone), rate);

    InstrumentRunner drums(std::make_unique<DrumSampler>(), rate);
    drums.samples.assign(DrumSampler::numPads, nullptr);
    drums.samples[0] = sample.get();
    drums.set("velocity", 0.0f);
    drums.set("pad1Key", 60.0f);
    drums.set("pad1Tune", 12.0f);
    drums.set("pad1Decay", 10000.0f);
    drums.set("pad1Pan", -100.0f);

    auto out = drums.run({midi::noteOn(0, 36, 1.0f)}, 1000);
    CHECK(out[500] == 0.0f);
    out = drums.run({midi::noteOn(drums.position, 60, 1.0f)}, 20000);
    CHECK(daw::test::measuredFrequency(out, 200, 19000, rate) == Catch::Approx(882.0).epsilon(1e-5));
    // Hard left: nothing on the right.
    float right = 0.0f;
    for (float value : drums.lastRight)
        right = std::max(right, std::abs(value));
    CHECK(right == 0.0f);
}

TEST_CASE("The drum sampler ignores the host's blocks", "[engine][drums]")
{
    const auto play = [](int blockSize) {
        Kit kit;
        std::vector<midi::Event> events;
        for (int i = 0; i < 60; ++i)
            events.push_back(midi::noteOn(i * 397, 36 + (i * 5) % 16, 0.3f + 0.01f * static_cast<float>(i)));
        auto out = kit.runner->run(events, 30000, blockSize);
        CHECK(kit.runner->heapOperations == 0);
        return out;
    };
    const auto reference = play(512);
    for (const int blockSize : {1, 64, 300})
    {
        const auto out = play(blockSize);
        bool identical = true;
        for (std::size_t i = 0; i < out.size(); ++i)
            identical = identical &&
                        std::bit_cast<std::uint32_t>(out[i]) == std::bit_cast<std::uint32_t>(reference[i]);
        CAPTURE(blockSize);
        CHECK(identical);
    }
}
