#include "daw/controller/MidiFiles.h"
#include "daw/controller/Session.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <cmath>
#include <vector>

namespace Catch
{
template <>
struct StringMaker<daw::model::Note>
{
    static std::string convert(const daw::model::Note& note)
    {
        return "{start " + std::to_string(note.start) + ", length " + std::to_string(note.length) +
               ", key " + std::to_string(note.key) + ", velocity " + std::to_string(note.velocity) + "}";
    }
};
} // namespace Catch

using Catch::Matchers::ContainsSubstring;
using daw::controller::MidiFileContent;
using daw::controller::readMidiFile;
using daw::controller::writeMidiFile;
using daw::model::ControllerEvent;
using daw::model::ControllerType;
using daw::model::Note;
using daw::model::Project;
using daw::model::Ticks;
using daw::model::TrackKind;

namespace
{

// A project with two MIDI tracks whose notes and controllers fall on the file's grid
// (a 960th of a quarter note), and an audio track the file leaves out.
Project songAt(double tempo, daw::model::TimeSignature metre)
{
    Project project;
    project.setTempo(tempo);
    project.setTimeSignature(metre);
    const double quarter = project.getTempoMap().ticksPerQuarterNote();
    const auto at = [quarter](int pulses) {
        return static_cast<Ticks>(std::llround(pulses * quarter / 960.0));
    };

    // Notes start and end on the grid (as snapped edits do); controllers are back at
    // rest at the clip's end, as the file has them.
    const auto note = [&at](int start, int length, int key, int velocity) {
        return Note{0, at(start), at(start + length) - at(start), key, velocity};
    };

    project.addTrack("Voice");
    auto keys = project.addTrack("Keys", TrackKind::midi);
    keys.setMidiChannel(3);
    std::vector<Note> notes;
    for (int i = 0; i < 40; ++i)
        notes.push_back(note(i * 457, 100 + (i * 131) % 3000, 30 + (i * 7) % 60, 1 + (i * 13) % 127));
    keys.addMidiClip("Keys", {0, 0, at(960 * 24)}, notes,
                     {{ControllerType::controlChange(1), at(0), 10},
                      {ControllerType::controlChange(1), at(1000), 90},
                      {ControllerType::controlChange(1), at(9000), 0},
                      {ControllerType::controlChange(64), at(500), 127},
                      {ControllerType::controlChange(64), at(5000), 0},
                      {ControllerType::pitchBend(), at(300), 0},
                      {ControllerType::pitchBend(), at(700), 16383},
                      {ControllerType::pitchBend(), at(900), 8192},
                      {ControllerType::channelPressure(), at(2000), 77},
                      {ControllerType::channelPressure(), at(3000), 0}});

    auto drums = project.addTrack("Drums", TrackKind::midi);
    drums.setMidiChannel(10);
    drums.addMidiClip("Drums", {0, 0, at(960 * 8)},
                      {note(0, 60, 36, 127), note(480, 60, 42, 80), note(960, 60, 38, 110)});
    return project;
}

// The notes and controllers a project's MIDI track plays, from its one clip.
struct Played
{
    std::vector<Note> notes;
    std::vector<ControllerEvent> controllers;
};

Played playedBy(const Project& project, int trackIndex)
{
    const auto clip = project.getTrack(trackIndex).getClip(0);
    const daw::model::MidiClip midi(clip.getState());
    auto notes = midi.getNotes();
    for (auto& note : notes)
        note.id = 0;
    return {notes, midi.getControllerEvents()};
}

std::vector<Note> withoutIds(std::vector<Note> notes)
{
    for (auto& note : notes)
        note.id = 0;
    return notes;
}

} // namespace

TEST_CASE("MIDI tracks go to a MIDI file and come back without a loss", "[controller][midi][file]")
{
    const int type = GENERATE(0, 1);
    const double tempo = GENERATE(120.0, 123.4);
    const auto project = songAt(tempo, {3, 4});
    const auto bytes = writeMidiFile(project, type);

    MidiFileContent content;
    REQUIRE(readMidiFile(bytes, project.getTempoMap(), content).wasOk());
    CHECK(content.type == type);
    CHECK(content.leftOut.isEmpty());
    CHECK(std::abs(content.tempo - tempo) < 0.001); // microseconds per quarter note
    CHECK(content.timeSignature == daw::model::TimeSignature{3, 4});
    REQUIRE(content.parts.size() == 2);

    // Every note, velocity and controller at its tick; channels and names.
    const auto keys = playedBy(project, 1);
    const auto drums = playedBy(project, 2);
    const auto& first = content.parts[0];
    const auto& second = content.parts[1];
    CHECK(first.channel == 3);
    CHECK(second.channel == 10);
    if (type == 1)
    {
        CHECK(first.name == "Keys");
        CHECK(second.name == "Drums");
    }
    CHECK(withoutIds(first.notes) == keys.notes);
    CHECK(first.controllers == keys.controllers);
    CHECK(withoutIds(second.notes) == drums.notes);

    // And again: the file written from what was read is the same file.
    Project again;
    again.setTempo(tempo);
    again.setTimeSignature({3, 4});
    for (const auto& part : content.parts)
    {
        auto track = again.addTrack(part.name, TrackKind::midi);
        track.setMidiChannel(part.channel);
        track.addMidiClip(part.name, {0, 0, part.end + 10}, part.notes, part.controllers);
    }
    if (type == 0)
    {
        // A type 0 file has no track names: name them as the first export did.
        again.getTrack(0).setName("Keys");
        again.getTrack(1).setName("Drums");
    }
    CHECK(writeMidiFile(again, type) == bytes);
}

TEST_CASE("What a track plays is what its file holds: cut notes, controllers at rest",
          "[controller][midi][file]")
{
    Project project;
    const double quarter = project.getTempoMap().ticksPerQuarterNote();
    const auto at = [quarter](int pulses) { return static_cast<Ticks>(std::llround(pulses * quarter / 960.0)); };
    auto track = project.addTrack("Keys", TrackKind::midi);
    // The clip shows its content from 960 on, for two quarters: the note before it is
    // hidden, the long one is cut at the clip's end; the pedal goes up there.
    track.addMidiClip("Keys", {at(960 * 4), at(960), at(1920)},
                      {{0, at(0), at(960), 50, 100}, {0, at(960), at(5000), 60, 100}},
                      {{ControllerType::controlChange(64), at(1000), 127}});

    MidiFileContent content;
    REQUIRE(readMidiFile(writeMidiFile(project, 1), project.getTempoMap(), content).wasOk());
    REQUIRE(content.parts.size() == 1);
    REQUIRE(content.parts[0].notes.size() == 1);
    CHECK(content.parts[0].notes[0].start == at(960 * 4));
    CHECK(content.parts[0].notes[0].length == at(1920));
    REQUIRE(content.parts[0].controllers.size() == 2);
    CHECK(content.parts[0].controllers[0].position == at(960 * 4 + 40));
    CHECK(content.parts[0].controllers[1].position == at(960 * 6));
    CHECK(content.parts[0].controllers[1].value == 0);
}

TEST_CASE("Notes struck again on their key are written as they sound", "[controller][midi][file]")
{
    Project project;
    const double quarter = project.getTempoMap().ticksPerQuarterNote();
    const auto at = [quarter](int pulses) { return static_cast<Ticks>(std::llround(pulses * quarter / 960.0)); };
    auto track = project.addTrack("Keys", TrackKind::midi);
    track.addMidiClip("Keys", {0, 0, at(960 * 8)},
                      {{0, at(0), at(960), 60, 100},
                       {0, at(480), at(960), 60, 90},  // ends the first at 480
                       {0, at(2000), at(100), 62, 80},
                       {0, at(2000), at(200), 62, 70}}); // the first never sounds

    MidiFileContent content;
    REQUIRE(readMidiFile(writeMidiFile(project, 1), project.getTempoMap(), content).wasOk());
    const auto notes = withoutIds(content.parts.at(0).notes);
    REQUIRE(notes.size() == 3);
    CHECK(notes[0] == Note{0, at(0), at(480), 60, 100});
    CHECK(notes[1] == Note{0, at(480), at(1440) - at(480), 60, 90});
    CHECK(notes[2] == Note{0, at(2000), at(2200) - at(2000), 62, 70});
}

TEST_CASE("MIDI files from elsewhere import with their tracks, channels and timing",
          "[controller][midi][file]")
{
    // A type 1 file at 480 PPQ: a tempo of 100 and 6/8, a tempo change later; a piano
    // track; a track with drums on channel 10 and a bass on channel 2, a program
    // change and a SysEx; a note-on never ended.
    juce::MidiFile file;
    file.setTicksPerQuarterNote(480);
    juce::MidiMessageSequence conductor;
    conductor.addEvent(juce::MidiMessage::tempoMetaEvent(600000), 0.0);
    conductor.addEvent(juce::MidiMessage::timeSignatureMetaEvent(6, 8), 0.0);
    conductor.addEvent(juce::MidiMessage::tempoMetaEvent(500000), 4800.0);
    file.addTrack(conductor);

    juce::MidiMessageSequence piano;
    piano.addEvent(juce::MidiMessage::textMetaEvent(3, "Piano"), 0.0);
    piano.addEvent(juce::MidiMessage::noteOn(1, 60, static_cast<juce::uint8>(100)), 240.0);
    piano.addEvent(juce::MidiMessage::noteOff(1, 60), 720.0);
    piano.addEvent(juce::MidiMessage::noteOn(1, 64, static_cast<juce::uint8>(90)), 960.0);
    piano.addEvent(juce::MidiMessage::endOfTrack(), 1920.0);
    file.addTrack(piano);

    juce::MidiMessageSequence band;
    band.addEvent(juce::MidiMessage::textMetaEvent(3, "Band"), 0.0);
    band.addEvent(juce::MidiMessage::programChange(10, 5), 0.0);
    const juce::uint8 sysex[] = {0x7e, 0x7f, 0x09, 0x01};
    band.addEvent(juce::MidiMessage::createSysExMessage(sysex, 4), 0.0);
    band.addEvent(juce::MidiMessage::noteOn(10, 36, static_cast<juce::uint8>(127)), 0.0);
    band.addEvent(juce::MidiMessage::noteOff(10, 36), 120.0);
    band.addEvent(juce::MidiMessage::noteOn(2, 40, static_cast<juce::uint8>(70)), 480.0);
    band.addEvent(juce::MidiMessage::noteOff(2, 40), 960.0);
    band.addEvent(juce::MidiMessage::pitchWheel(2, 10000), 600.0);
    file.addTrack(band);

    daw::test::TemporaryDirectory directory;
    const auto path = directory.file("song.mid");
    {
        juce::FileOutputStream stream(path);
        REQUIRE(file.writeTo(stream, 1));
    }

    daw::engine::Engine engine;
    engine.prepare(48000.0, 512);
    daw::controller::Session session(engine, {daw::engine::io::DiskStreamer::Mode::manual});
    const auto import = session.importMidi(path, 0);
    REQUIRE(import.result.wasOk());
    CHECK(session.getUndoDescription() == "Import MIDI File");

    // The project took the file's first tempo and metre.
    const auto& project = session.getProject();
    CHECK(std::abs(project.getTempoMap().getTempo() - 100.0) < 1e-9);
    CHECK(project.getTempoMap().getTimeSignature() == daw::model::TimeSignature{6, 8});
    const auto leftOut = import.leftOut.joinIntoString("; ").toStdString();
    CHECK_THAT(leftOut, ContainsSubstring("1 tempo change"));
    CHECK_THAT(leftOut, ContainsSubstring("1 program changes"));
    CHECK_THAT(leftOut, ContainsSubstring("1 system exclusive messages"));

    REQUIRE(project.getNumTracks() == 3);
    CHECK(project.getTrack(0).getName() == "Piano");
    CHECK(project.getTrack(1).getName() == "Band (Ch 2)");
    CHECK(project.getTrack(1).getMidiChannel() == 2);
    CHECK(project.getTrack(2).getName() == "Band (Ch 10)");
    CHECK(project.getTrack(2).getInstrument()->getType() == "drums");
    CHECK(project.getTrack(0).getInstrument()->getType() == "synth");

    // Beats at the project's tempo: 240 pulses of 480 = an eighth note.
    const double quarter = project.getTempoMap().ticksPerQuarterNote();
    const auto piano0 = playedBy(project, 0);
    REQUIRE(piano0.notes.size() == 2);
    CHECK(piano0.notes[0].start == static_cast<Ticks>(std::llround(quarter / 2.0)));
    CHECK(piano0.notes[0].length == static_cast<Ticks>(std::llround(quarter * 1.5)) -
                                        static_cast<Ticks>(std::llround(quarter / 2.0)));
    // The note never ended lasts to its track's end.
    CHECK(piano0.notes[1].start + piano0.notes[1].length ==
          static_cast<Ticks>(std::llround(quarter * 4.0)));
    // Its clip runs to the bar line after it (bars of 6/8: three quarters).
    CHECK(project.getTrack(0).getClip(0).getLength() ==
          project.getTempoMap().barStart(2) - project.getTempoMap().barStart(0));

    // Written again at 960 PPQ and read back: the same notes (480 divides 960).
    MidiFileContent content;
    REQUIRE(readMidiFile(writeMidiFile(project, 1), project.getTempoMap(), content).wasOk());
    REQUIRE(content.parts.size() == 3);
    CHECK(withoutIds(content.parts[0].notes) == piano0.notes);
    // The bend, and back to the centre where the clip ends.
    REQUIRE(content.parts[1].controllers.size() == 2);
    CHECK(content.parts[1].controllers[0].value == 10000);
    CHECK(content.parts[1].controllers[1].value == 8192);

    REQUIRE(session.undo());
    CHECK(session.getProject().getNumTracks() == 0);
    CHECK(session.getProject().getTempoMap().getTempo() == 120.0);

    const auto notMidi = directory.file("not.mid");
    REQUIRE(notMidi.replaceWithText("hello"));
    CHECK(session.importMidi(notMidi, 0).result.failed());
}
