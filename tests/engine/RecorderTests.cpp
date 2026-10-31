#include "daw/engine/Engine.h"
#include "support/SimulatedDevice.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <bit>
#include <cstdint>
#include <memory>
#include <vector>

using daw::engine::Arrangement;
using daw::engine::Engine;
using daw::engine::InputMonitoring;
using daw::engine::RecordingSetup;
using daw::engine::RecordingState;
using daw::engine::TransportState;
using daw::engine::rt::AudioFifo;
using daw::test::SimulatedDevice;
using daw::test::testInputSample;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int fadeLength = 240; // the transport's fade-in, 5 ms

// Every sample different, so that any misalignment shows.
float rampSample(juce::int64 position)
{
    return static_cast<float>(position + 1) / 1048576.0f;
}

const std::shared_ptr<const daw::engine::MemoryAudioSource>& ramp()
{
    static const auto source = [] {
        juce::AudioBuffer<float> samples(1, 400000);
        for (int i = 0; i < samples.getNumSamples(); ++i)
            samples.setSample(0, i, rampSample(i));
        return daw::test::makeSource(std::move(samples), sampleRate);
    }();
    return source;
}

// Track 1 plays the ramp from 0; track 2 (armed, no clips) is what records, from
// device input 0 unless told otherwise.
std::unique_ptr<Arrangement> rampAndRecordTrack(InputMonitoring monitoring = InputMonitoring::off)
{
    auto arrangement = daw::test::makeWholeFileArrangement(ramp(), 1);
    daw::engine::ArrangementTrack recording;
    recording.id = 2;
    recording.inputChannel = 0;
    recording.numInputChannels = 1;
    recording.monitoring = monitoring;
    recording.armed = true;
    arrangement->tracks.push_back(recording);
    return arrangement;
}

std::vector<float> popAll(AudioFifo& fifo, int channel = 0)
{
    const auto ready = static_cast<std::size_t>(fifo.getNumReady());
    std::vector<std::vector<float>> channels(static_cast<std::size_t>(fifo.getNumChannels()),
                                             std::vector<float>(ready));
    std::vector<float*> pointers;
    for (auto& samples : channels)
        pointers.push_back(samples.data());
    fifo.pop(pointers.data(), fifo.getNumReady());
    return channels[static_cast<std::size_t>(channel)];
}

bool sameBits(float a, float b)
{
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

// What was played at timeline position startPosition + k, k samples after playback
// started there: the ramp, faded in.
float playedFrom(juce::int64 startPosition, juce::int64 k)
{
    const float sample = rampSample(startPosition + k);
    return k < fadeLength
               ? sample * (static_cast<float>(k + 1) / static_cast<float>(fadeLength))
               : sample;
}

} // namespace

TEST_CASE("Recordings line up to the sample with what was played", "[engine][recorder]")
{
    const auto blockSizes = GENERATE(std::vector<int>{64}, std::vector<int>{1, 7, 512, 100},
                                     std::vector<int>{2048});
    const int extraLatency = GENERATE(0, 1, 37, 4096);
    const int largestBlock = *std::max_element(blockSizes.begin(), blockSizes.end());
    const int latency = largestBlock + extraLatency;
    CAPTURE(blockSizes, latency);

    Engine engine;
    SimulatedDevice device(engine, sampleRate, 2, blockSizes);
    device.setStreamStart((juce::int64{1} << 32) - 3000);
    device.connectLoopback(0, 0, latency); // a cable from output 1 to input 1
    device.start();
    engine.setArrangement(rampAndRecordTrack());

    AudioFifo fifo(1, 400000);
    RecordingSetup setup;
    setup.targets.push_back({2, 0, 1, &fifo});
    setup.startPosition = 20000;
    setup.latency = latency;
    engine.startRecording(setup);

    std::size_t heapOperations = device.run(100000);
    CHECK(engine.getRecordingStatus().state == RecordingState::recording);
    engine.stop();
    heapOperations += device.run(latency + 2 * largestBlock);
    CHECK(heapOperations == 0);

    const auto status = engine.getRecordingStatus();
    REQUIRE(status.state == RecordingState::complete);
    CHECK_FALSE(status.overrun);
    CHECK(status.firstPosition == 20000);
    CHECK(status.samplesCaptured == status.stopPosition - status.firstPosition);
    CHECK(status.stopPosition > 20000 + 90000);

    // Sample k of the recording is what was played at 20000 + k: bit for bit.
    const auto recorded = popAll(fifo);
    REQUIRE(static_cast<juce::int64>(recorded.size()) == status.samplesCaptured);
    bool aligned = true;
    for (std::size_t k = 0; k < recorded.size(); ++k)
        aligned = aligned && sameBits(recorded[k], playedFrom(20000, static_cast<juce::int64>(k)));
    CHECK(aligned);
}

TEST_CASE("A wrong latency shows as a shift of exactly that many samples", "[engine][recorder]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 1, {128});
    device.connectLoopback(0, 0, 500);
    device.start();
    engine.setArrangement(rampAndRecordTrack());

    AudioFifo fifo(1, 100000);
    RecordingSetup setup;
    setup.targets.push_back({2, 0, 1, &fifo});
    setup.startPosition = 1000;
    setup.latency = 497; // three samples short
    engine.startRecording(setup);
    device.run(20000);
    engine.stop();
    device.run(1000);

    const auto recorded = popAll(fifo);
    REQUIRE(recorded.size() > 10000);
    CHECK(recorded[3 + 5000] == playedFrom(1000, 5000));
    CHECK(recorded[5000] != playedFrom(1000, 5000));
}

TEST_CASE("Several tracks record at once from mono and stereo inputs", "[engine][recorder]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 4, {256, 31});
    device.inputSignal = testInputSample;
    device.start();

    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = sampleRate; // an empty project: recording starts anyway
    engine.setArrangement(std::move(arrangement));

    AudioFifo mono(1, 100000);
    AudioFifo stereo(2, 100000);
    RecordingSetup setup;
    setup.targets.push_back({1, 1, 1, &mono});
    setup.targets.push_back({2, 2, 2, &stereo});
    setup.startPosition = 5000;
    setup.latency = 300;
    engine.startRecording(setup);

    // The recording starts with the first block: stream index 0.
    CHECK(device.run(30000) == 0);
    CHECK(engine.getPlayheadPosition() > 5000 + 29000);
    engine.stop();
    device.run(1000);

    const auto status = engine.getRecordingStatus();
    REQUIRE(status.state == RecordingState::complete);
    const auto recordedMono = popAll(mono);
    const auto recordedLeft = popAll(stereo, 0);
    REQUIRE(recordedMono.size() == static_cast<std::size_t>(status.samplesCaptured));
    REQUIRE(recordedLeft.size() == recordedMono.size());

    bool exact = true;
    for (std::size_t k = 0; k < recordedMono.size(); ++k)
    {
        const auto index = 300 + static_cast<juce::int64>(k);
        exact = exact && sameBits(recordedMono[k], testInputSample(1, index)) &&
                sameBits(recordedLeft[k], testInputSample(2, index));
    }
    CHECK(exact);
}

TEST_CASE("The count-in is captured before the start position", "[engine][recorder]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 1, {100});
    device.connectLoopback(0, 0, 100);
    device.start();
    engine.setArrangement(rampAndRecordTrack());

    AudioFifo fifo(1, 100000);
    RecordingSetup setup;
    setup.targets.push_back({2, 0, 1, &fifo});
    setup.startPosition = 20000;
    setup.countInLength = 4321;
    setup.latency = 100;
    engine.startRecording(setup);

    device.run(2000);
    CHECK(engine.isCountingIn());
    device.run(20000);
    engine.stop();
    device.run(300);

    const auto status = engine.getRecordingStatus();
    REQUIRE(status.state == RecordingState::complete);
    CHECK(status.firstPosition == 20000 - 4321);

    // Silence while counting in (no metronome here, the timeline muted), then the
    // timeline from the start position.
    const auto recorded = popAll(fifo);
    CHECK(std::all_of(recorded.begin(), recorded.begin() + 4321,
                      [](float x) { return x == 0.0f; }));
    bool aligned = true;
    for (std::size_t k = 4321; k < recorded.size(); ++k)
        aligned = aligned &&
                  sameBits(recorded[k], playedFrom(20000, static_cast<juce::int64>(k) - 4321));
    CHECK(aligned);
}

TEST_CASE("While punching, automatic monitoring hears the input exactly between punch in and out",
          "[engine][recorder]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 1, {512, 3, 1000});
    device.inputSignal = [](int, juce::int64) { return 0.5f; };
    device.setRecordsOutput(true);
    device.start();

    auto arrangement = daw::test::makeWholeFileArrangement(
        daw::test::makeConstantSource(1, 100000, 0.25f, sampleRate));
    auto& track = arrangement->tracks.front();
    track.inputChannel = 0;
    track.numInputChannels = 1;
    track.monitoring = InputMonitoring::automatic;
    track.armed = true;
    engine.setArrangement(std::move(arrangement));
    device.run(2000); // stopped and armed: hears the input

    AudioFifo fifo(1, 100000);
    RecordingSetup setup;
    setup.targets.push_back({1, 0, 1, &fifo});
    setup.startPosition = 0;
    setup.punchIn = 10000;
    setup.punchOut = 20000;
    engine.startRecording(setup);
    CHECK(device.run(30000) == 0);

    // The recording started at stream index 2000, at timeline 0.
    const auto& output = device.getOutput(0);
    const auto at = [&](juce::int64 position) {
        return output[static_cast<std::size_t>(2000 + position)];
    };
    const auto up = [](int i) { return static_cast<float>(i + 1) / 240.0f; };
    const auto down = [](int i) {
        return 1.0f + (0.0f - 1.0f) * (static_cast<float>(i + 1) / 240.0f);
    };

    CHECK(at(9999) == 0.25f);
    bool exact = true;
    for (int i = 0; i < 240; ++i)
    {
        exact = exact && sameBits(at(10000 + i), 0.25f * down(i) + 0.5f * (0.0f + 1.0f * up(i)));
        exact = exact && sameBits(at(20000 + i), 0.25f * (0.0f + 1.0f * up(i)) + 0.5f * down(i));
    }
    CHECK(exact);
    CHECK(at(10240) == 0.5f);
    CHECK(at(19999) == 0.5f);
    CHECK(at(20240) == 0.25f);
}

TEST_CASE("A full FIFO ends the capture at the exact sample, never blocking", "[engine][recorder]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 1, {64});
    device.inputSignal = testInputSample;
    device.start();
    engine.setArrangement(rampAndRecordTrack());

    AudioFifo fifo(1, 1000); // nobody empties it
    RecordingSetup setup;
    setup.targets.push_back({2, 0, 1, &fifo});
    engine.startRecording(setup);

    CHECK(device.run(10000) == 0);
    const auto status = engine.getRecordingStatus();
    CHECK(status.state == RecordingState::complete);
    CHECK(status.overrun);
    CHECK(status.samplesCaptured == 960); // 15 whole blocks; the 16th did not fit
    CHECK(fifo.getNumReady() == 960);
    CHECK(engine.getTransportState() == TransportState::playing); // the session stops it
}

TEST_CASE("Recording runs straight on: no loop, no end, no seeks; stop goes back to the start",
          "[engine][recorder]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 1, {500});
    device.start();

    auto arrangement = daw::test::makeWholeFileArrangement(
        daw::test::makeConstantSource(1, 5000, 0.25f, sampleRate));
    arrangement->loopStart = 1000;
    arrangement->loopEnd = 3000;
    engine.setArrangement(std::move(arrangement));
    engine.setLooping(true);

    AudioFifo fifo(1, 100000);
    RecordingSetup setup;
    setup.targets.push_back({1, 0, 1, &fifo});
    setup.startPosition = 2000;
    engine.startRecording(setup);

    device.run(10000);
    CHECK(engine.getPlayheadPosition() == 12000); // past the loop end and the timeline's end
    engine.seek(0);
    engine.play();
    device.run(500);
    CHECK(engine.getPlayheadPosition() == 12500);

    engine.pause(); // stops the recording
    device.run(1000);
    CHECK(engine.getRecordingStatus().state == RecordingState::complete);
    CHECK(engine.getRecordingStatus().stopPosition == 12500);
    CHECK(engine.getTransportState() == TransportState::stopped);
    CHECK(engine.getPlayheadPosition() == 2000);

    // Afterwards the loop works again.
    engine.play();
    device.run(4500); // from 2000: 1000 to the loop end, 2000 once round, 1500 more
    CHECK(engine.getPlayheadPosition() == 2500);
    CHECK(engine.isLooping());
}

TEST_CASE("A recording waits for the transport to stop, and a device restart ends it",
          "[engine][recorder]")
{
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 1, {100});
    device.start();
    engine.setArrangement(rampAndRecordTrack());
    engine.play();
    device.run(1000);

    AudioFifo fifo(1, 100000);
    RecordingSetup setup;
    setup.targets.push_back({2, 0, 1, &fifo});
    setup.startPosition = 7000;
    engine.stop();
    engine.startRecording(setup);
    CHECK(engine.getRecordingStatus().state == RecordingState::starting);

    device.run(100); // the stop's fade-out: 240 samples
    CHECK(engine.getRecordingStatus().state == RecordingState::starting);
    device.run(300);
    CHECK(engine.getRecordingStatus().state == RecordingState::recording);
    CHECK(engine.getRecordingStatus().firstPosition == 7000);

    device.run(1000);
    engine.prepare(sampleRate, 100); // the device restarts
    const auto status = engine.getRecordingStatus();
    CHECK(status.state == RecordingState::complete);
    CHECK(status.interrupted);
    CHECK(status.stopPosition == status.firstPosition + status.samplesCaptured);
}
