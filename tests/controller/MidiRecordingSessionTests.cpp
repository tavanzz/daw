#include "daw/controller/MidiTakes.h"
#include "daw/controller/Session.h"

#include "support/SimulatedDevice.h"
#include "support/TestAudioFiles.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>

#include <memory>
#include <vector>

namespace midi = daw::engine::midi;
using daw::controller::Session;
using daw::controller::SessionOptions;
using daw::model::ControllerType;
using daw::model::Note;
using daw::model::ObjectId;
using daw::model::Ticks;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr Ticks sample = 5880;
constexpr int outputLatency = 200;

SessionOptions testOptions()
{
    SessionOptions options;
    options.effects = [](const juce::String& type) -> std::unique_ptr<daw::engine::fx::Effect> {
        if (type == "test-instrument")
            return std::make_unique<daw::test::TestInstrument>();
        return nullptr;
    };
    return options;
}

struct Studio
{
    Studio()
    {
        device.setReportedLatencies(0, outputLatency);
        device.start();
        session.setSampleRate(sampleRate);
        session.setDeviceLatency(0, outputLatency);
    }

    std::size_t run(juce::int64 numSamples)
    {
        std::size_t heapOperations = 0;
        for (juce::int64 done = 0; done < numSamples; done += blockSize)
        {
            heapOperations += device.run(std::min<juce::int64>(blockSize, numSamples - done));
            session.update();
        }
        return heapOperations;
    }

    bool finishRecording()
    {
        // The writer finishes the files on its own thread: give it real time (a slow
        // machine, a sanitizer build), up to about four seconds.
        for (int i = 0; i < 2000 && session.isRecording(); ++i)
        {
            run(blockSize);
            if (session.isRecording())
                juce::Thread::sleep(2);
        }
        return !session.isRecording();
    }

    daw::engine::Engine engine;
    daw::test::SimulatedDevice device{engine, sampleRate, 2, {blockSize}};
    Session session{engine, testOptions()};
    daw::test::TemporaryDirectory directory;
};

daw::model::Track trackOf(const Session& session, ObjectId trackId)
{
    return session.getProject().getTrack(session.getProject().indexOfTrack(trackId));
}

} // namespace

TEST_CASE("A MIDI take becomes notes held to their note-offs, and controllers",
          "[controller][midi][recording]")
{
    daw::controller::RecordedMidiTake take;
    take.firstPosition = 1000;
    take.events = {{0, 1000, midi::noteOn(0, 60, midi::fromSevenBit(100))},
                   {0, 1100, midi::noteOn(0, 64, midi::fromSevenBit(80), 2)},
                   {0, 1200, midi::controller(0, 64, 1.0f)},
                   {0, 1500, midi::noteOff(0, 60)},
                   {0, 1600, midi::pitchBend(0, midi::fromPitchBend(12000))},
                   {0, 1700, midi::noteOn(0, 60, midi::fromSevenBit(1))},
                   {0, 1700, midi::noteOff(0, 60)},
                   {0, 1800, midi::noteOff(0, 64, 3)}, // another channel: not 64's off
                   {0, 1900, midi::noteOn(0, 67, 0.5f)}};

    const auto content = daw::controller::midiTakeContent(take, 2500, sampleRate);
    REQUIRE(content.notes.size() == 4);
    const auto withoutId = [](Note note) {
        note.id = 0;
        return note;
    };
    CHECK(withoutId(content.notes[0]) == Note{0, 0, 500 * sample, 60, 100});
    CHECK(withoutId(content.notes[1]) == Note{0, 100 * sample, 1400 * sample, 64, 80}); // to the stop
    CHECK(withoutId(content.notes[2]) == Note{0, 700 * sample, 1, 60, 1}); // as short as can be
    CHECK(content.notes[3].start == 900 * sample);
    CHECK(content.notes[3].length == 600 * sample);
    CHECK(content.notes[3].velocity == 64);

    REQUIRE(content.controllers.size() == 2);
    CHECK(content.controllers[0].type == ControllerType::controlChange(64));
    CHECK(content.controllers[0].value == 127);
    CHECK(content.controllers[0].position == 200 * sample);
    CHECK(content.controllers[1].type == ControllerType::pitchBend());
    CHECK(content.controllers[1].value == 12000);
}

TEST_CASE("MIDI recordings land at the exact ticks that were heard, in one undo step",
          "[controller][midi][recording]")
{
    Studio studio;
    auto& session = studio.session;
    session.setTempo(120.0); // a bar is 96000 samples
    const auto keys = session.addMidiTrack("Keys", "test-instrument");
    REQUIRE(session.setTrackArmed(keys, true));
    session.setCountInEnabled(true);
    session.seek(2 * 96000 * sample); // bar 3
    studio.run(blockSize);

    // MIDI alone needs no saved project.
    const auto undoBefore = session.getUndoDescription();
    REQUIRE(session.record().wasOk());
    const auto start = studio.device.getStreamPosition();
    const juce::int64 first = 96000; // the count-in's bar
    const auto heardAt = [&](juce::int64 position) {
        return start + outputLatency + (position - first);
    };

    // A note during the count-in, held into the recording; one on the downbeat; a
    // pedal; and a note still down when recording stops.
    studio.device.playMidi(heardAt(190000), juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)));
    studio.device.playMidi(heardAt(192000 + 100), juce::MidiMessage::noteOff(1, 60));
    studio.device.playMidi(heardAt(192000), juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(90)));
    studio.device.playMidi(heardAt(192000 + 4800), juce::MidiMessage::noteOff(1, 64));
    studio.device.playMidi(heardAt(196000), juce::MidiMessage::controllerEvent(1, 64, 127));
    studio.device.playMidi(heardAt(200000), juce::MidiMessage::noteOn(1, 67, static_cast<juce::uint8>(70)));
    const auto heap = studio.run(heardAt(210000) - studio.device.getStreamPosition());
    CHECK(heap == 0);
    session.stop();
    REQUIRE(studio.finishRecording());

    const auto track = trackOf(session, keys);
    REQUIRE(track.getNumClips() == 1);
    const auto clip = track.getClip(0);
    CHECK(clip.isMidi());
    CHECK(clip.getName() == "Keys");
    // The clip shows the recording from the start (bar 3); the count-in's bar is
    // hidden in it, as a handle.
    CHECK(clip.getPosition() == 192000 * sample);
    CHECK(clip.getOffset() == 96000 * sample);

    const daw::model::MidiClip midiClip(clip.getState());
    const auto notes = midiClip.getNotes();
    REQUIRE(notes.size() == 3);
    const auto timeline = [&](const Note& note) { return clip.getPosition() - clip.getOffset() + note.start; };
    CHECK(timeline(notes[0]) == 190000 * sample);
    CHECK(notes[0].length == 2100 * sample);
    CHECK(timeline(notes[1]) == 192000 * sample);
    CHECK(notes[1].length == 4800 * sample);
    CHECK(notes[1].velocity == 90);
    CHECK(timeline(notes[2]) == 200000 * sample);
    CHECK(timeline(notes[2]) + notes[2].length == clip.getPosition() + *clip.getLength());
    const auto pedal = midiClip.getControllerEvents(ControllerType::controlChange(64));
    REQUIRE(pedal.size() == 1);
    CHECK(clip.getPosition() - clip.getOffset() + pedal[0].position == 196000 * sample);

    CHECK(session.getUndoDescription() == "Record");
    REQUIRE(session.undo());
    CHECK(trackOf(session, keys).getNumClips() == 0);
    CHECK(session.getUndoDescription() == undoBefore);
}

TEST_CASE("Audio and MIDI record together; nothing played adds no MIDI clip",
          "[controller][midi][recording]")
{
    Studio studio;
    auto& session = studio.session;
    const auto voice = session.addTrack("Voice");
    const auto keys = session.addMidiTrack("Keys", "test-instrument");
    const auto silent = session.addMidiTrack("Silent", "test-instrument");
    session.setTrackMidiInput(silent, {"Elsewhere", 0}); // not connected
    session.setMidiInputDevices({"Keys"}, [](const juce::String& name) { return name == "Keys" ? 0 : -1; });
    session.setTrackArmed(voice, true);
    session.setTrackArmed(keys, true);
    session.setTrackArmed(silent, true);

    // Audio needs a saved project.
    CHECK(session.record().failed());
    REQUIRE(session.saveProject(studio.directory.file("song.dawproject")).wasOk());
    REQUIRE(session.record().wasOk());
    const auto start = studio.device.getStreamPosition();
    studio.device.playMidi(start + outputLatency + 1000, juce::MidiMessage::noteOn(1, 60, 1.0f));
    studio.device.playMidi(start + outputLatency + 2000, juce::MidiMessage::noteOff(1, 60));
    studio.run(48000);
    session.stop();
    REQUIRE(studio.finishRecording());

    CHECK(trackOf(session, voice).getNumClips() == 1);
    CHECK(trackOf(session, keys).getNumClips() == 1);
    CHECK(trackOf(session, silent).getNumClips() == 0);
    REQUIRE(session.undo());
    CHECK(trackOf(session, voice).getNumClips() == 0);
    CHECK(trackOf(session, keys).getNumClips() == 0);

    // Discarded: nothing at all.
    REQUIRE(session.record().wasOk());
    studio.device.playMidi(studio.device.getStreamPosition() + outputLatency + 1000,
                           juce::MidiMessage::noteOn(1, 62, 1.0f));
    studio.run(48000);
    session.discardRecording();
    REQUIRE(studio.finishRecording());
    CHECK(trackOf(session, keys).getNumClips() == 0);
    CHECK(trackOf(session, voice).getNumClips() == 0);
}
