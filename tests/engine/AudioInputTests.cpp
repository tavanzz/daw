#include "daw/engine/Engine.h"
#include "daw/engine/dsp/InputMeters.h"
#include "support/SimulatedDevice.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <vector>

using daw::engine::AudioInputs;
using daw::engine::Engine;
using daw::test::SimulatedDevice;

namespace
{

constexpr double sampleRate = 48000.0;

// A different constant level on every input channel: 0.01 × (channel + 1).
float levelOf(int channel)
{
    return 0.01f * static_cast<float>(channel + 1);
}

} // namespace

TEST_CASE("Input meters measure each channel and reset when read", "[engine][input]")
{
    std::array<float, 64> left{};
    std::array<float, 64> right{};
    left.fill(0.25f);
    right[10] = -0.75f;
    const float* channels[] = {left.data(), nullptr, right.data()};

    daw::engine::dsp::InputMeters meters;
    meters.process(AudioInputs{channels, 3}, 64);

    CHECK(meters.takePeak(0) == 0.25f);
    CHECK(meters.takePeak(1) == 0.0f);
    CHECK(meters.takePeak(2) == 0.75f);
    CHECK(meters.takePeak(0) == 0.0f);
    CHECK(meters.takePeak(-1) == 0.0f);
    CHECK(meters.takePeak(AudioInputs::maxChannels) == 0.0f);
}

TEST_CASE("The device callback gives the engine each input at its channel number",
          "[engine][input][device]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 8, {256, 100, 512});
    device.inputSignal = [](int channel, juce::int64) { return levelOf(channel); };

    // JUCE delivers only the inputs that are on, packed: 0, 2, 5 and 7 arrive as
    // callback channels 0 to 3.
    for (const int off : {1, 3, 4, 6})
        device.setInputEnabled(off, false);
    device.start();

    CHECK(engine.getSampleRate() == sampleRate);
    CHECK(device.run(4096) == 0);

    for (int channel = 0; channel < 8; ++channel)
    {
        const bool on = channel == 0 || channel == 2 || channel == 5 || channel == 7;
        CHECK(engine.takeInputPeak(channel) == (on ? levelOf(channel) : 0.0f));
    }
}

TEST_CASE("Inputs beyond the engine's maximum are left out", "[engine][input][device]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, AudioInputs::maxChannels + 6, {128});
    device.inputSignal = [](int channel, juce::int64) { return levelOf(channel); };
    device.start();

    CHECK(device.run(512) == 0);
    CHECK(engine.takeInputPeak(0) == levelOf(0));
    CHECK(engine.takeInputPeak(AudioInputs::maxChannels - 1) ==
          levelOf(AudioInputs::maxChannels - 1));
}

TEST_CASE("A device without inputs still plays", "[engine][input][device]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {256});
    device.setRecordsOutput(true);
    device.start();

    engine.setArrangement(daw::test::makeWholeFileArrangement(
        daw::test::makeConstantSource(2, 48000, 0.5f, sampleRate)));
    engine.play();

    CHECK(device.run(2048) == 0);
    CHECK(device.getOutput(0).back() == 0.5f);
    CHECK(engine.takeInputPeak(0) == 0.0f);
}

TEST_CASE("The simulated device loops output back to an input with a known latency",
          "[engine][input][device]")
{
    constexpr int latency = 777;
    constexpr juce::int64 streamStart = (juce::int64{1} << 32) - 1000;

    Engine engine;
    SimulatedDevice device(engine, sampleRate, 2, {64, 512, 1, 300});
    device.setStreamStart(streamStart);
    device.connectLoopback(0, 1, latency);
    device.setRecordsOutput(true);
    device.setRecordsInput(true);
    device.start();

    // A ramp, so that every sample is different.
    juce::AudioBuffer<float> ramp(2, 20000);
    for (int i = 0; i < ramp.getNumSamples(); ++i)
        for (int channel = 0; channel < 2; ++channel)
            ramp.setSample(channel, i, static_cast<float>(i + 1) / 32768.0f);
    engine.setArrangement(
        daw::test::makeWholeFileArrangement(daw::test::makeSource(ramp, sampleRate)));
    engine.play();

    CHECK(device.run(10000) == 0);
    CHECK(device.getStreamPosition() == streamStart + 10000);

    const auto& played = device.getOutput(0);
    const auto& returned = device.getInput(1);
    REQUIRE(returned.size() == played.size());

    constexpr auto delay = static_cast<std::size_t>(latency);
    bool aligned = true;
    for (std::size_t i = 0; i < returned.size(); ++i)
    {
        const float expected = i >= delay ? played[i - delay] : 0.0f;
        aligned = aligned && std::bit_cast<std::uint32_t>(returned[i]) ==
                                 std::bit_cast<std::uint32_t>(expected);
    }
    CHECK(aligned);
    CHECK(device.getInput(0).back() == 0.0f);
}
