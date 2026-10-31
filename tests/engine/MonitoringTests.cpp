#include "daw/engine/Engine.h"
#include "daw/engine/OfflineRenderer.h"
#include "support/SimulatedDevice.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <vector>

using daw::engine::Arrangement;
using daw::engine::Engine;
using daw::engine::InputMonitoring;
using daw::test::SimulatedDevice;
using daw::test::testInputSample;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int rampLength = 240; // 5 ms at 48 kHz: monitoring switches and transport fades
constexpr float clipLevel = 0.25f;

// A constant clip of a second. Arrangements that share it play the same: swapping
// them is not crossfaded.
const std::shared_ptr<const daw::engine::MemoryAudioSource>& clip()
{
    static const auto source = daw::test::makeConstantSource(2, 48000, clipLevel, sampleRate);
    return source;
}

// One track playing the clip, with its input settings.
std::unique_ptr<Arrangement> track(InputMonitoring monitoring, bool armed, int inputChannel = 0,
                                   int numInputChannels = 1)
{
    auto arrangement = daw::test::makeWholeFileArrangement(clip());
    auto& first = arrangement->tracks.front();
    first.monitoring = monitoring;
    first.armed = armed;
    first.inputChannel = inputChannel;
    first.numInputChannels = numInputChannels;
    return arrangement;
}

bool sameBits(float a, float b)
{
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// The gain of a monitoring ramp from 0 to 1 at sample i of the ramp, as GainRamp
// computes it.
float rampUp(int i)
{
    return 0.0f + (1.0f - 0.0f) * (static_cast<float>(i + 1) / static_cast<float>(rampLength));
}

float rampDown(int i)
{
    return 1.0f + (0.0f - 1.0f) * (static_cast<float>(i + 1) / static_cast<float>(rampLength));
}

struct Rig
{
    explicit Rig(std::vector<int> blockSizes = {256, 100, 1, 512}, int numInputs = 4)
        : device(engine, sampleRate, numInputs, std::move(blockSizes))
    {
        device.setRecordsOutput(true);
        device.setRecordsInput(true);
    }

    std::size_t run(juce::int64 numSamples) { return heapOperations += device.run(numSamples); }

    [[nodiscard]] float out(int channel, std::size_t i) const
    {
        return device.getOutput(channel)[i];
    }

    [[nodiscard]] float in(int channel, std::size_t i) const { return device.getInput(channel)[i]; }

    Engine engine;
    SimulatedDevice device;
    std::size_t heapOperations = 0;
};

} // namespace

TEST_CASE("A monitored input is heard through the track's gains, bit for bit",
          "[engine][monitoring]")
{
    Rig rig;
    rig.device.inputSignal = testInputSample;
    rig.device.start();

    auto arrangement = track(InputMonitoring::on, false);
    arrangement->tracks.front().leftGain = 0.5f;
    arrangement->tracks.front().rightGain = 0.125f;
    rig.engine.setArrangement(std::move(arrangement));

    rig.run(10000);
    CHECK(rig.heapOperations == 0);

    // It fades in over 5 ms when the track starts monitoring, then passes unchanged
    // but for the track gains. The transport is stopped: no clip is heard.
    bool exact = true;
    for (std::size_t i = 0; i < 10000; ++i)
    {
        const float input = rig.in(0, i);
        const float ramped = i < rampLength ? input * rampUp(static_cast<int>(i)) : input;
        exact = exact && sameBits(rig.out(0, i), ramped * 0.5f) &&
                sameBits(rig.out(1, i), ramped * 0.125f);
    }
    CHECK(exact);
}

TEST_CASE("Play, pause and stop never fade a monitored input", "[engine][monitoring]")
{
    Rig rig;
    rig.device.inputSignal = testInputSample;
    rig.device.start();
    rig.engine.setArrangement(track(InputMonitoring::on, false));

    rig.run(1000);
    rig.engine.play();
    rig.run(3000);
    rig.engine.seek(1000);
    rig.run(1000);
    rig.engine.pause();
    rig.run(1000);
    rig.engine.play();
    rig.run(1000);
    rig.engine.stop();
    rig.run(1000);
    CHECK(rig.heapOperations == 0);

    bool exact = true;
    for (std::size_t i = rampLength; i < 8000; ++i)
        exact = exact && sameBits(rig.out(0, i), rig.in(0, i)) &&
                sameBits(rig.out(1, i), rig.in(0, i));
    CHECK(exact);
}

TEST_CASE("Automatic monitoring hears an armed track's input while stopped, its clips while "
          "playing",
          "[engine][monitoring]")
{
    Rig rig({256});
    rig.device.inputSignal = [](int, juce::int64) { return 0.5f; };
    rig.device.start();
    rig.engine.setArrangement(track(InputMonitoring::automatic, true));

    rig.run(2048);
    CHECK(rig.out(0, 2047) == 0.5f); // stopped and armed: the input

    rig.engine.play();
    rig.run(2048);
    CHECK(rig.out(0, 4095) == clipLevel); // playing: the clip

    rig.engine.stop();
    rig.run(2048);
    CHECK(rig.out(1, 6143) == 0.5f);

    // Disarmed: neither (stopped), with a 5 ms ramp down.
    rig.engine.setArrangement(track(InputMonitoring::automatic, false));
    rig.run(2048);
    CHECK(rig.out(0, 6144 + rampLength - 2) > 0.0f);
    CHECK(rig.out(0, 6144 + rampLength - 1) == 0.0f);
    CHECK(rig.out(0, 8191) == 0.0f);

    // Monitoring off: never, even armed.
    rig.engine.setArrangement(track(InputMonitoring::off, true));
    rig.run(2048);
    CHECK(rig.out(0, 10239) == 0.0f);
    rig.engine.play();
    rig.run(2048);
    CHECK(rig.out(0, 12287) == clipLevel);

    // Monitoring on: always, even playing.
    rig.engine.setArrangement(track(InputMonitoring::on, false));
    rig.run(2048);
    CHECK(rig.out(0, 14335) == 0.5f);
    CHECK(rig.heapOperations == 0);
}

TEST_CASE("Switching between input and clips is an exact 5 ms crossfade", "[engine][monitoring]")
{
    Rig rig({256});
    rig.device.inputSignal = [](int, juce::int64) { return 0.5f; };
    rig.device.start();
    rig.engine.setArrangement(track(InputMonitoring::on, false));
    rig.engine.play();
    rig.run(2048);

    // Monitoring off while playing: the input fades out as the clip fades in, from
    // the first sample of the next block.
    rig.engine.setArrangement(track(InputMonitoring::off, false));
    rig.run(1024);
    CHECK(rig.heapOperations == 0);

    bool exact = true;
    for (int i = 0; i < 1024; ++i)
    {
        const float expected = i < rampLength
                                   ? clipLevel * rampUp(i) + 0.5f * rampDown(i)
                                   : clipLevel;
        exact = exact && sameBits(rig.out(0, 2048 + static_cast<std::size_t>(i)), expected);
    }
    CHECK(exact);
}

TEST_CASE("Stereo inputs keep their sides and mono inputs are heard on both",
          "[engine][monitoring]")
{
    Rig rig({128}, 4);
    rig.device.inputSignal = [](int channel, juce::int64) { return 0.1f * (channel + 1); };
    rig.device.start();

    rig.engine.setArrangement(track(InputMonitoring::on, false, 2, 2));
    rig.run(1024);
    CHECK(rig.out(0, 1023) == 0.3f);
    CHECK(rig.out(1, 1023) == 0.4f);

    rig.engine.setArrangement(track(InputMonitoring::on, false, 3, 1));
    rig.run(1024);
    CHECK(rig.out(0, 2047) == 0.4f);
    CHECK(rig.out(1, 2047) == 0.4f);

    // An input the device does not have is silence.
    rig.engine.setArrangement(track(InputMonitoring::on, false, 5, 1));
    rig.run(1024);
    CHECK(rig.out(0, 3071) == 0.0f);

    // So is a muted track's input.
    auto muted = track(InputMonitoring::on, false, 0, 1);
    muted->tracks.front().leftGain = 0.0f;
    muted->tracks.front().rightGain = 0.0f;
    rig.engine.setArrangement(std::move(muted));
    rig.run(2048);
    CHECK(rig.out(0, 5119) == 0.0f);
    CHECK(rig.out(1, 5119) == 0.0f);
    CHECK(rig.heapOperations == 0);
}

TEST_CASE("Offline renders never hear an input", "[engine][monitoring]")
{
    const auto arrangement = track(InputMonitoring::on, true);
    daw::engine::OfflineRenderer renderer(*arrangement);
    juce::AudioBuffer<float> output(2, 1000);
    renderer.render(output);

    CHECK(output.getSample(0, 0) == clipLevel);
    CHECK(output.getSample(1, 999) == clipLevel);
}
