#include "daw/engine/Engine.h"
#include "daw/engine/Recorder.h"

#include "support/MidiArrangements.h"
#include "support/SimulatedDevice.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <memory>
#include <vector>

namespace midi = daw::engine::midi;
using daw::engine::Engine;
using daw::engine::InputMonitoring;
using daw::engine::MidiRecordingQueue;
using daw::engine::RecordedMidi;
using daw::engine::RecordingSetup;
using daw::engine::RecordingState;
using daw::test::makeMidiArrangement;
using daw::test::makeTestInstrument;
using daw::test::SimulatedDevice;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int effectLatency = 64;
constexpr juce::int64 outputLatency = 300;

std::vector<RecordedMidi> drain(MidiRecordingQueue& queue)
{
    std::vector<RecordedMidi> events;
    while (const auto event = queue.pop())
        events.push_back(*event);
    return events;
}

// A MIDI track (id 1, armed) whose chain delays the audio by effectLatency samples.
std::unique_ptr<daw::engine::Arrangement> recordingProject(const daw::test::HostedInstrument& instrument)
{
    auto arrangement = makeMidiArrangement(instrument, {}, 1000);
    auto& track = arrangement->tracks[0];
    track.monitoring = InputMonitoring::automatic;
    track.armed = true;
    daw::engine::ChainSlot latency;
    daw::engine::fx::ProcessSetup setup;
    setup.sampleRate = sampleRate;
    latency.instance = std::make_shared<daw::engine::fx::EffectInstance>(
        std::make_unique<daw::test::LatencyEffect>(effectLatency), setup);
    track.inserts.slots.push_back(latency);
    return arrangement;
}

} // namespace

TEST_CASE("MIDI recordings place each event where it was heard, to the sample",
          "[engine][midi][recording]")
{
    const std::vector<int> blocks = GENERATE(std::vector<int>{256}, std::vector<int>{128, 256, 64});

    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, blocks);
    device.start();
    engine.setArrangement(recordingProject(instrument));
    REQUIRE(device.run(1024) == 0);
    REQUIRE(engine.getLatency() == effectLatency);

    auto queue = std::make_unique<MidiRecordingQueue>();
    RecordingSetup setup;
    setup.midiTargets = {{1, {}}};
    setup.midi = queue.get();
    setup.midiLatency = outputLatency;
    setup.startPosition = 48000;
    setup.countInLength = 9600;
    const juce::int64 first = setup.startPosition - setup.countInLength;

    // The recording starts with the next block; position p is heard (out of the
    // device) at that block + the effects' latency + the output latency + p - first.
    const juce::int64 start = device.getStreamPosition();
    const auto heardAt = [&](juce::int64 position) {
        return start + effectLatency + outputLatency + (position - first);
    };
    engine.startRecording(setup);

    device.playMidi(heardAt(40000), juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(90)));
    device.playMidi(heardAt(49000), juce::MidiMessage::noteOff(1, 60));
    device.playMidi(heardAt(50000), juce::MidiMessage::controllerEvent(1, 1, 64));
    device.playMidi(heardAt(53000), juce::MidiMessage::noteOn(1, 62, static_cast<juce::uint8>(100)));
    device.playMidi(heardAt(55000), juce::MidiMessage::noteOff(1, 62));
    REQUIRE(device.run(heardAt(57000) - device.getStreamPosition()) == 0);

    // Stopped at the next block: the stop is heard effectLatency later. A key played
    // just before that is kept, even though it arrives after the stop; one just
    // after is not.
    engine.stop();
    const juce::int64 stopStream = device.getStreamPosition();
    device.playMidi(stopStream + effectLatency + outputLatency - 10,
                    juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(80)));
    device.playMidi(stopStream + effectLatency + outputLatency + 10,
                    juce::MidiMessage::noteOn(1, 65, static_cast<juce::uint8>(80)));
    REQUIRE(device.run(4096) == 0);

    const auto status = engine.getRecordingStatus();
    REQUIRE(status.state == RecordingState::complete);
    REQUIRE_FALSE(status.midiOverrun);

    const auto events = drain(*queue);
    REQUIRE(events.size() == 6);
    CHECK(events[0].position == 40000);
    CHECK(events[0].event.type == midi::EventType::noteOn);
    CHECK(midi::toSevenBit(events[0].event.value) == 90);
    CHECK(events[1].position == 49000);
    CHECK(events[2].position == 50000);
    CHECK(events[2].event == midi::controller(events[2].event.offset, 1, midi::fromSevenBit(64)));
    CHECK(events[3].position == 53000);
    CHECK(events[4].position == 55000);
    CHECK(events[5].event.number == 64);
    CHECK(events[5].position == status.stopPosition - 10);
    for (const auto& event : events)
        CHECK(event.target == 0);
    engine.collectGarbage();
}

TEST_CASE("Each MIDI target records what its filter lets through", "[engine][midi][recording]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {256});
    device.start();
    engine.setArrangement(recordingProject(instrument));
    REQUIRE(device.run(512) == 0);

    auto queue = std::make_unique<MidiRecordingQueue>();
    RecordingSetup setup;
    setup.midiTargets = {{1, {0, -1}}, {2, {-1, 2}}};
    setup.midi = queue.get();
    setup.startPosition = 0;
    engine.startRecording(setup);

    device.playMidi(2000, juce::MidiMessage::noteOn(1, 60, 1.0f), 0); // target 0
    device.playMidi(2000, juce::MidiMessage::noteOn(3, 61, 1.0f), 1); // target 1
    device.playMidi(2000, juce::MidiMessage::noteOn(3, 62, 1.0f), 0); // both
    device.playMidi(2000, juce::MidiMessage::noteOn(1, 63, 1.0f), 1); // neither
    REQUIRE(device.run(3000) == 0);
    engine.stop();
    REQUIRE(device.run(2048) == 0);
    REQUIRE(engine.getRecordingStatus().state == RecordingState::complete);

    std::vector<std::pair<int, int>> targetsAndKeys;
    for (const auto& event : drain(*queue))
        targetsAndKeys.emplace_back(event.target, event.event.number);
    const std::vector<std::pair<int, int>> expected{{0, 60}, {1, 61}, {0, 62}, {1, 62}};
    CHECK(targetsAndKeys == expected);
    engine.collectGarbage();
}
