#include "daw/engine/Engine.h"
#include "daw/engine/midi/LiveInput.h"

#include "support/MidiArrangements.h"
#include "support/SimulatedDevice.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <memory>
#include <vector>

namespace midi = daw::engine::midi;
using daw::engine::Engine;
using daw::engine::InputMonitoring;
using daw::test::HostedInstrument;
using daw::test::makeMidiArrangement;
using daw::test::makeNote;
using daw::test::makeTestInstrument;
using daw::test::SimulatedDevice;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

std::unique_ptr<daw::engine::Arrangement> listeningTrack(const HostedInstrument& instrument,
                                                         InputMonitoring monitoring, bool armed,
                                                         midi::InputFilter filter = {})
{
    auto arrangement = makeMidiArrangement(instrument, {}, 100000);
    auto& track = arrangement->tracks[0];
    track.monitoring = monitoring;
    track.armed = armed;
    track.midiInput = filter;
    return arrangement;
}

// The first and last sample (from from) at which the output is level.
std::pair<std::size_t, std::size_t> spanOf(const std::vector<float>& samples, float level,
                                           std::size_t from = 0)
{
    std::size_t first = samples.size();
    std::size_t last = 0;
    for (std::size_t i = from; i < samples.size(); ++i)
        if (samples[i] == level)
        {
            first = std::min(first, i);
            last = i;
        }
    return {first, last};
}

} // namespace

TEST_CASE("Live MIDI parses into events, note-ons with velocity 0 as note-offs",
          "[engine][midi]")
{
    midi::Event event;
    midi::TimedMessage message;
    message.size = 3;
    message.bytes = {0x93, 60, 100};
    REQUIRE(midi::parse(message, event));
    CHECK(event.type == midi::EventType::noteOn);
    CHECK(event.channel == 3);
    CHECK(midi::toSevenBit(event.value) == 100);

    message.bytes = {0x90, 60, 0};
    REQUIRE(midi::parse(message, event));
    CHECK(event.type == midi::EventType::noteOff);

    message.bytes = {0xe1, 0x00, 0x40};
    REQUIRE(midi::parse(message, event));
    CHECK(event.type == midi::EventType::pitchBend);
    CHECK(event.value == 0.0f);

    message.bytes = {0xb0, 64, 127};
    REQUIRE(midi::parse(message, event));
    CHECK(event == midi::controller(event.offset, 64, 1.0f));

    message.size = 2;
    message.bytes = {0xd2, 90, 0};
    REQUIRE(midi::parse(message, event));
    CHECK(event.type == midi::EventType::channelPressure);

    message.bytes = {0xc0, 5, 0}; // program change
    REQUIRE_FALSE(midi::parse(message, event));
    message.size = 1;
    message.bytes = {0xf8, 0, 0}; // clock
    REQUIRE_FALSE(midi::parse(message, event));
}

TEST_CASE("Live notes play one block after they were played, keeping their timing",
          "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.setRecordsOutput(true);
    device.start();
    engine.setArrangement(listeningTrack(instrument, InputMonitoring::automatic, true));

    device.playMidi(1000, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)));
    device.playMidi(1100, juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(127)));
    device.playMidi(1300, juce::MidiMessage::noteOff(1, 60));
    device.playMidi(2000, juce::MidiMessage::noteOff(1, 64));
    REQUIRE(device.run(4000) == 0);

    const auto& out = device.getOutput(0);
    for (std::size_t i = 0; i < out.size(); ++i)
    {
        float expected = 0.0f;
        if (i >= 1000 + blockSize && i < 1300 + blockSize)
            expected += 1.0f;
        if (i >= 1100 + blockSize && i < 2000 + blockSize)
            expected += 1.0f;
        REQUIRE(out[i] == expected);
    }
    REQUIRE(instrument.probe->getHeldCount() == 0);
    REQUIRE(instrument.probe->wellFormed);
    engine.collectGarbage();
}

TEST_CASE("A MIDI track hears its input as its monitoring says", "[engine][midi]")
{
    const auto monitoring = GENERATE(InputMonitoring::off, InputMonitoring::automatic,
                                     InputMonitoring::on);
    const bool armed = GENERATE(false, true);

    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.setRecordsOutput(true);
    device.start();
    engine.setArrangement(listeningTrack(instrument, monitoring, armed));

    device.playMidi(500, juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(127)));
    device.playMidi(900, juce::MidiMessage::noteOff(1, 60));
    REQUIRE(device.run(2000) == 0);

    const bool hears = monitoring == InputMonitoring::on ||
                       (monitoring == InputMonitoring::automatic && armed);
    const auto [first, last] = spanOf(device.getOutput(0), 1.0f);
    CAPTURE(static_cast<int>(monitoring), armed);
    if (hears)
    {
        CHECK(first == 500 + blockSize);
        CHECK(last == 900 + blockSize - 1);
    }
    else
    {
        CHECK(first == device.getOutput(0).size());
    }
    CHECK(instrument.probe->getHeldCount() == 0);
    engine.collectGarbage();
}

TEST_CASE("A MIDI track hears only the device and channel it listens to", "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.setRecordsOutput(true);
    device.start();
    engine.setArrangement(listeningTrack(instrument, InputMonitoring::on, false, {1, 2}));

    device.playMidi(100, juce::MidiMessage::noteOn(3, 60, 1.0f), 1);  // channel 3: heard
    device.playMidi(100, juce::MidiMessage::noteOn(3, 62, 1.0f), 0);  // another device
    device.playMidi(100, juce::MidiMessage::noteOn(4, 64, 1.0f), 1);  // another channel
    device.playMidi(600, juce::MidiMessage::noteOff(3, 60), 1);
    device.playMidi(600, juce::MidiMessage::noteOff(3, 62), 0);
    device.playMidi(600, juce::MidiMessage::noteOff(4, 64), 1);
    REQUIRE(device.run(1500) == 0);

    const auto& received = instrument.probe->received;
    REQUIRE(received.size() == 2);
    CHECK(received[0].event.number == 60);
    CHECK(received[1].event.type == midi::EventType::noteOff);
    engine.collectGarbage();
}

TEST_CASE("Live notes end when the track stops listening, the pedal goes up",
          "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.setRecordsOutput(true);
    device.start();
    engine.setArrangement(listeningTrack(instrument, InputMonitoring::automatic, true));

    device.playMidi(100, juce::MidiMessage::controllerEvent(1, 64, 127));
    device.playMidi(200, juce::MidiMessage::noteOn(1, 60, 1.0f));
    device.playMidi(200, juce::MidiMessage::noteOn(1, 67, 1.0f));
    REQUIRE(device.run(1024) == 0);
    REQUIRE(instrument.probe->getHeldCount() == 2);

    // Disarmed while the keys are down: they end at the next block, and the pedal
    // with them; the note-offs that come later go nowhere.
    engine.setArrangement(listeningTrack(instrument, InputMonitoring::automatic, false));
    device.playMidi(1500, juce::MidiMessage::noteOff(1, 60));
    REQUIRE(device.run(2048) == 0);

    CHECK(instrument.probe->getHeldCount() == 0);
    CHECK(instrument.probe->wellFormed);
    const auto& received = instrument.probe->received;
    REQUIRE(received.size() == 6);
    CHECK(received[3].sample == 1024);
    CHECK(received[3].event.type == midi::EventType::noteOff);
    CHECK(received[4].event.type == midi::EventType::noteOff);
    CHECK(received[5].event == midi::controller(received[5].event.offset, 64, 0.0f));
    engine.collectGarbage();
}

TEST_CASE("Previews play on their track at the next block, whatever the monitoring",
          "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    auto other = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.setRecordsOutput(true);
    device.start();
    auto arrangement = listeningTrack(instrument, InputMonitoring::off, false);
    auto second = makeMidiArrangement(other, {}, 100000, sampleRate, 2);
    arrangement->tracks.push_back(std::move(second->tracks[0]));
    engine.setArrangement(std::move(arrangement));

    REQUIRE(device.run(blockSize) == 0);
    engine.startPreview(1, 60, 0.5f);
    REQUIRE(device.run(blockSize * 2) == 0);
    engine.stopPreview(1, 60);
    REQUIRE(device.run(blockSize * 2) == 0);

    const auto [first, last] = spanOf(device.getOutput(0), 0.5f);
    CHECK(first == blockSize);
    CHECK(last == 3 * blockSize - 1);
    CHECK(other.probe->received.empty());
    CHECK(instrument.probe->getHeldCount() == 0);

    // A track removed while previewing lets its note go.
    engine.startPreview(1, 62, 0.5f);
    REQUIRE(device.run(blockSize) == 0);
    REQUIRE(instrument.probe->getHeldCount() == 1);
    engine.setArrangement(makeMidiArrangement(other, {}, 100000, sampleRate, 2));
    REQUIRE(device.run(blockSize * 2) == 0);
    CHECK(instrument.probe->getHeldCount() == 0);
    engine.collectGarbage();
}

namespace
{
// Whether every note-on the instrument got was ended by a note-off of the same key and
// channel (and note id), none left over, and no note-off for a note it did not have.
bool everyNoteEnded(const daw::test::TestInstrument& probe)
{
    std::vector<midi::Event> sounding;
    for (const auto& received : probe.received)
    {
        const auto& event = received.event;
        if (event.type == midi::EventType::noteOn)
        {
            sounding.push_back(event);
        }
        else if (event.type == midi::EventType::noteOff)
        {
            const auto match = std::find_if(sounding.begin(), sounding.end(), [&](const auto& note) {
                return note.number == event.number && note.channel == event.channel &&
                       (event.noteId < 0 || note.noteId == event.noteId);
            });
            if (match == sounding.end())
                return false;
            sounding.erase(match);
        }
    }
    return sounding.empty() && probe.wellFormed && probe.getHeldCount() == 0;
}
} // namespace

TEST_CASE("Fast preview drags end every note they start", "[engine][midi]")
{
    // As the piano roll previews a dragged note: the old key off, the new key on, as
    // often as the mouse moves, several times between two blocks, the button released
    // anywhere (even in the block its last key started).
    const int changesPerBlock = GENERATE(1, 2, 3);
    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.start();
    engine.setArrangement(listeningTrack(instrument, InputMonitoring::off, false));
    REQUIRE(device.run(blockSize) == 0);

    juce::Random random(changesPerBlock);
    int key = -1;
    for (int block = 0; block < 64; ++block)
    {
        for (int change = 0; change < changesPerBlock; ++change)
        {
            if (key >= 0)
                engine.stopPreview(1, key);
            key = 48 + random.nextInt(24);
            engine.startPreview(1, key, 0.5f);
        }
        if (block % 8 == 7) // the button goes up
        {
            engine.stopPreview(1, key);
            key = -1;
        }
        REQUIRE(device.run(blockSize) == 0);
    }
    if (key >= 0)
        engine.stopPreview(1, key);
    REQUIRE(device.run(blockSize * 2) == 0);

    CHECK(instrument.probe->getHeldCount() == 0);
    CHECK(everyNoteEnded(*instrument.probe));
    engine.collectGarbage();
}

TEST_CASE("Stop, pause, bypass and a track not heard end preview notes", "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.start();
    engine.setArrangement(listeningTrack(instrument, InputMonitoring::off, false));
    REQUIRE(device.run(blockSize) == 0);

    SECTION("stop and pause, also while stopped; previews play again after")
    {
        engine.startPreview(1, 60, 0.5f);
        REQUIRE(device.run(blockSize) == 0);
        REQUIRE(instrument.probe->getHeldCount() == 1);
        engine.stop();
        REQUIRE(device.run(blockSize) == 0);
        CHECK(instrument.probe->getHeldCount() == 0);

        engine.startPreview(1, 62, 0.5f);
        REQUIRE(device.run(blockSize) == 0);
        CHECK(instrument.probe->getHeldCount() == 1);
        engine.play();
        engine.pause();
        REQUIRE(device.run(blockSize) == 0);
        CHECK(instrument.probe->getHeldCount() == 0);
        engine.stopPreview(1, 62); // the mouse goes up later: nothing to end
        REQUIRE(device.run(blockSize) == 0);
    }
    SECTION("a preview ended at once, every track or one")
    {
        engine.startPreview(1, 60, 0.5f);
        engine.startPreview(1, 64, 0.5f);
        REQUIRE(device.run(blockSize) == 0);
        REQUIRE(instrument.probe->getHeldCount() == 2);
        engine.stopAllPreviews(2);
        REQUIRE(device.run(blockSize) == 0);
        CHECK(instrument.probe->getHeldCount() == 2);
        engine.stopAllPreviews(1);
        REQUIRE(device.run(blockSize) == 0);
        CHECK(instrument.probe->getHeldCount() == 0);
        engine.startPreview(1, 60, 0.5f);
        engine.stopAllPreviews(midi::LiveEvent::everyTrack);
        REQUIRE(device.run(blockSize) == 0);
        CHECK(instrument.probe->getHeldCount() == 0);
    }
    SECTION("a track not heard (muted, or another soloed)")
    {
        engine.startPreview(1, 60, 0.5f);
        REQUIRE(device.run(blockSize) == 0);
        REQUIRE(instrument.probe->getHeldCount() == 1);
        auto muted = listeningTrack(instrument, InputMonitoring::off, false);
        muted->tracks[0].heard = false;
        engine.setArrangement(std::move(muted));
        REQUIRE(device.run(blockSize) == 0);
        CHECK(instrument.probe->getHeldCount() == 0);
        engine.startPreview(1, 62, 0.5f);
        REQUIRE(device.run(blockSize) == 0);
        CHECK(instrument.probe->getHeldCount() == 0);
        engine.stopPreview(1, 62);
    }
    SECTION("the instrument bypassed")
    {
        engine.startPreview(1, 60, 0.5f);
        REQUIRE(device.run(blockSize) == 0);
        REQUIRE(instrument.probe->getHeldCount() == 1);
        auto bypassed = listeningTrack(instrument, InputMonitoring::off, false);
        bypassed->tracks[0].inserts.slots[0].enabled = false;
        engine.setArrangement(std::move(bypassed));
        REQUIRE(device.run(blockSize * 8) == 0);
        CHECK(instrument.probe->getHeldCount() == 0);
        engine.stopPreview(1, 60);
    }
    REQUIRE(device.run(blockSize * 2) == 0);
    CHECK(everyNoteEnded(*instrument.probe));
    engine.collectGarbage();
}

TEST_CASE("All notes off from a device ends the notes it holds, and goes no further",
          "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.start();
    engine.setArrangement(listeningTrack(instrument, InputMonitoring::on, false));

    device.playMidi(100, juce::MidiMessage::noteOn(1, 60, 1.0f));
    device.playMidi(100, juce::MidiMessage::noteOn(1, 64, 1.0f));
    device.playMidi(1000, juce::MidiMessage::allNotesOff(1));
    device.playMidi(1500, juce::MidiMessage::noteOn(1, 67, 1.0f));
    device.playMidi(1600, juce::MidiMessage::allSoundOff(1));
    device.playMidi(1700, juce::MidiMessage::noteOff(1, 60)); // already ended
    REQUIRE(device.run(3000) == 0);

    CHECK(instrument.probe->getHeldCount() == 0);
    CHECK(everyNoteEnded(*instrument.probe));
    for (const auto& received : instrument.probe->received)
        CHECK(received.event.type != midi::EventType::controller);
    engine.collectGarbage();
}

TEST_CASE("Panic stops every voice and forgets every note; playback goes on", "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.setRecordsOutput(true);
    device.start();
    auto arrangement = listeningTrack(instrument, InputMonitoring::on, false);
    arrangement->tracks[0].notes = {makeNote(0, 4000, 48, 0.25f, 1), makeNote(3000, 3500, 50, 0.25f, 2)};
    engine.setArrangement(std::move(arrangement));
    engine.play();
    device.playMidi(100, juce::MidiMessage::noteOn(1, 60, 1.0f));
    engine.startPreview(1, 64, 0.5f);
    REQUIRE(device.run(2048) == 0);
    REQUIRE(instrument.probe->getHeldCount() == 3);
    const int resets = instrument.probe->resets;

    engine.panic();
    REQUIRE(device.run(blockSize) == 0);
    CHECK(instrument.probe->resets == resets + 1);
    CHECK(instrument.probe->getHeldCount() == 0);

    // The held key, the preview and the timeline note end later: nothing more to end.
    device.playMidi(2500, juce::MidiMessage::noteOff(1, 60));
    engine.stopPreview(1, 64);
    REQUIRE(device.run(4000 - 2048 - blockSize) == 0);
    CHECK(instrument.probe->wellFormed);

    // The note starting after the panic played; the one started before did not again.
    const auto& out = device.getOutput(0);
    CHECK(out[2048 + blockSize] == 0.0f);
    CHECK(out[3200] == 0.25f);
    CHECK(out[3600] == 0.0f);
    CHECK(instrument.probe->getHeldCount() == 0);
    engine.collectGarbage();
}

TEST_CASE("Live input plays along with the timeline's notes", "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    SimulatedDevice device(engine, sampleRate, 0, {blockSize});
    device.setRecordsOutput(true);
    device.start();
    auto arrangement = listeningTrack(instrument, InputMonitoring::on, false);
    arrangement->tracks[0].notes = {makeNote(0, 3000, 48, 0.25f, 1)};
    engine.setArrangement(std::move(arrangement));
    engine.play();

    device.playMidi(1000, juce::MidiMessage::noteOn(1, 60, 1.0f));
    device.playMidi(2000, juce::MidiMessage::noteOff(1, 60));
    REQUIRE(device.run(3500) == 0);

    const auto& out = device.getOutput(0);
    CHECK(out[1000 + blockSize - 1] == 0.25f);
    CHECK(out[1000 + blockSize] == 1.25f);
    CHECK(out[2000 + blockSize] == 0.25f);
    CHECK(out[3000] == 0.0f);
    CHECK(instrument.probe->wellFormed);
    engine.collectGarbage();
}
