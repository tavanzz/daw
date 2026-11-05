#include "daw/engine/Sample.h"
#include "daw/engine/fx/Sampler.h"

#include "support/InstrumentRunner.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

namespace midi = daw::engine::midi;
using daw::engine::Sample;
using daw::engine::fx::Sampler;
using daw::test::InstrumentRunner;
using daw::test::measuredFrequency;

namespace
{

std::shared_ptr<const Sample> sineSample(double frequency, double rate, double seconds)
{
    const auto length = static_cast<int>(seconds * rate);
    juce::AudioBuffer<float> audio(1, length);
    for (int i = 0; i < length; ++i)
        audio.setSample(0, i, static_cast<float>(std::sin(2.0 * std::numbers::pi * frequency * i / rate)));
    return Sample::fromAudio(std::move(audio), rate);
}

std::unique_ptr<InstrumentRunner> samplerWith(const Sample& sample, double rate = 48000.0)
{
    auto runner = std::make_unique<InstrumentRunner>(std::make_unique<Sampler>(), rate);
    runner->samples = {&sample};
    runner->set("velocity", 0.0f);
    runner->set("attack", 0.1f);
    return runner;
}

double cents(double measured, double expected)
{
    return 1200.0 * std::log2(measured / expected);
}

float largestStep(const std::vector<float>& samples, std::size_t from, std::size_t to)
{
    float largest = 0.0f;
    for (std::size_t i = from + 1; i < to; ++i)
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    return largest;
}

} // namespace

TEST_CASE("Keys transpose the sample to their pitch, up and down", "[engine][sampler]")
{
    // The sample may come at another rate than the device's.
    const double sampleRate = GENERATE(48000.0, 44100.0);
    const auto sample = sineSample(440.0, sampleRate, 2.0);

    for (int key = 24; key <= 96; key += 5)
    {
        auto sampler = samplerWith(*sample);
        sampler->set("rootKey", 60.0f);
        const auto out = sampler->run({midi::noteOn(0, key, 1.0f)}, 24000);
        const double expected = 440.0 * std::exp2((key - 60) / 12.0);
        const auto end = std::min<std::size_t>(out.size(), static_cast<std::size_t>(2.0 * 48000.0 / std::exp2((key - 60) / 12.0)) - 100);
        const double measured = measuredFrequency(out, 1000, end, 48000.0);
        CAPTURE(sampleRate, key, measured, expected);
        CHECK(std::abs(cents(measured, expected)) < 0.1);
    }
}

TEST_CASE("Transpose, fine tune and the root key move the pitch exactly", "[engine][sampler]")
{
    const auto sample = sineSample(440.0, 48000.0, 2.0);
    auto sampler = samplerWith(*sample);
    sampler->set("rootKey", 69.0f); // A3 plays the sample as it is
    sampler->set("transpose", -12.0f);
    sampler->set("fine", 25.0f);
    const auto out = sampler->run({midi::noteOn(0, 76, 1.0f)}, 48000);
    CHECK(std::abs(cents(measuredFrequency(out, 1000, 47000, 48000.0),
                         440.0 * std::exp2((76 - 69 - 12 + 0.25) / 12.0))) < 0.1);
}

TEST_CASE("A crossfaded loop has no seam; without the crossfade it clicks", "[engine][sampler]")
{
    const auto sample = sineSample(440.0, 48000.0, 1.0);
    const float normalStep = static_cast<float>(2.0 * std::numbers::pi * 440.0 / 48000.0);

    const auto loopFor = [&](float crossfadeMs, float mode) {
        auto sampler = samplerWith(*sample);
        sampler->set("loop", mode);
        sampler->set("loopStart", 30.3f); // not a whole number of periods
        sampler->set("loopEnd", 70.1f);
        sampler->set("crossfade", crossfadeMs);
        return sampler->run({midi::noteOn(0, 60, 1.0f)}, 48000 * 3);
    };

    const auto smooth = loopFor(10.0f, 1.0f);
    CHECK(largestStep(smooth, 100, smooth.size()) < normalStep * 1.1f);
    CHECK(std::abs(smooth.back()) + std::abs(smooth[smooth.size() - 2]) > 0.0f); // still playing

    const auto clicks = loopFor(0.0f, 1.0f);
    CHECK(largestStep(clicks, 100, clicks.size()) > normalStep * 1.5f);

    const auto pingPong = loopFor(0.0f, 2.0f);
    CHECK(largestStep(pingPong, 100, pingPong.size()) < normalStep * 1.1f);
}

TEST_CASE("Without a loop a voice ends with its sample; a new sample silences the old",
          "[engine][sampler]")
{
    const auto shortSample = sineSample(440.0, 48000.0, 0.1);
    auto sampler = samplerWith(*shortSample);
    sampler->set("release", 5000.0f);
    auto& instrument = dynamic_cast<Sampler&>(*sampler->effect);
    const auto out = sampler->run({midi::noteOn(0, 60, 1.0f)}, 6000);
    CHECK(instrument.getActiveVoices() == 0);
    CHECK(out[4700] != 0.0f);
    CHECK(out[5000] == 0.0f);

    const auto longSample = sineSample(440.0, 48000.0, 2.0);
    sampler->samples = {longSample.get()};
    sampler->run({midi::noteOn(sampler->position, 60, 1.0f)}, 1000);
    CHECK(instrument.getActiveVoices() == 1);
    const auto other = sineSample(220.0, 48000.0, 2.0);
    sampler->samples = {other.get()};
    sampler->run({}, 16);
    CHECK(instrument.getActiveVoices() == 0);

    // No sample: no sound, no voice.
    sampler->samples = {nullptr};
    sampler->run({midi::noteOn(sampler->position, 60, 1.0f)}, 100);
    CHECK(instrument.getActiveVoices() == 0);
}

TEST_CASE("The sampler takes voices without allocating and ignores the host's blocks",
          "[engine][sampler]")
{
    const auto sample = sineSample(330.0, 48000.0, 1.0);
    const auto play = [&](int blockSize) {
        auto sampler = samplerWith(*sample);
        sampler->set("polyphony", 3.0f);
        sampler->set("loop", 1.0f);
        sampler->set("loopStart", 20.0f);
        sampler->set("loopEnd", 80.0f);
        std::vector<midi::Event> events;
        for (int i = 0; i < 40; ++i)
            events.push_back(midi::noteOn(i * 300, 40 + i, 0.5f, 0, i + 1));
        for (int i = 0; i < 40; ++i)
            events.push_back(midi::noteOff(20000 + i * 10, 40 + i, 0, i + 1));
        events.push_back(midi::pitchBend(5001, 0.4f));
        auto out = sampler->run(events, 40000, blockSize);
        CHECK(sampler->heapOperations == 0);
        return out;
    };

    const auto reference = play(512);
    for (const int blockSize : {1, 13, 100})
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
