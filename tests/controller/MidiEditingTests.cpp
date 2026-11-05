#include "daw/controller/Session.h"
#include "daw/model/ProjectFile.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using daw::controller::Session;
using daw::controller::SessionOptions;
using daw::engine::Engine;
using daw::model::ControllerEvent;
using daw::model::ControllerType;
using daw::model::Note;
using daw::model::Ticks;
using daw::model::ticksPerSecond;

namespace
{

struct Fixture
{
    Fixture()
    {
        engine.prepare(48000.0, 512);
    }

    // The whole project as its file would hold it.
    [[nodiscard]] juce::String snapshot() const
    {
        return daw::model::toJson(session.getProject(), juce::File::getCurrentWorkingDirectory());
    }

    // Runs edit, then checks that undo gives back the project before it and redo the
    // one after it, each in one step.
    template <typename Edit>
    void checkUndoable(const juce::String& name, Edit&& edit)
    {
        const auto before = snapshot();
        edit();
        const auto after = snapshot();
        CAPTURE(name);
        REQUIRE(after != before);
        CHECK(session.getUndoDescription() == name);
        REQUIRE(session.undo());
        CHECK(snapshot() == before);
        REQUIRE(session.redo());
        CHECK(snapshot() == after);
    }

    Engine engine;
    Session session{engine, SessionOptions{daw::engine::io::DiskStreamer::Mode::manual}};
};

daw::model::Track trackOf(const Session& session, daw::model::ObjectId trackId)
{
    return session.getProject().getTrack(session.getProject().indexOfTrack(trackId));
}

} // namespace

TEST_CASE("MIDI tracks and clips are added, moved, trimmed, copied and deleted with undo",
          "[controller][midi]")
{
    Fixture fixture;
    auto& session = fixture.session;

    daw::model::ObjectId keys = 0;
    fixture.checkUndoable("Add MIDI Track", [&] { keys = session.addMidiTrack(); });
    CHECK(trackOf(session, keys).isMidi());
    CHECK(trackOf(session, keys).getName() == "MIDI 1");
    const auto bass = session.addMidiTrack("Bass", {});
    const auto audio = session.addTrack();

    daw::model::ObjectId clip = 0;
    fixture.checkUndoable("New MIDI Clip", [&] { clip = session.addMidiClip(keys, 1000, 5000); });
    CHECK(session.addMidiClip(audio, 0, 100) == 0);
    REQUIRE(session.findMidiClip(clip).has_value());
    CHECK(session.findMidiClip(clip)->getClip().getName() == "MIDI 1");

    fixture.checkUndoable("Rename Clip", [&] { session.renameClip(clip, "Verse"); });
    fixture.checkUndoable("Move Clip", [&] { session.moveClip(clip, bass, 2000); });
    CHECK(trackOf(session, bass).getNumClips() == 1);

    // A MIDI clip never lands on an audio track.
    const auto before = fixture.snapshot();
    session.moveClip(clip, audio, 0);
    CHECK(session.copyClip(clip, audio, 0) == 0);
    CHECK(fixture.snapshot() == before);

    fixture.checkUndoable("Trim Clip", [&] { session.setClipBounds(clip, {2500, 500, 3000}); });

    std::vector<daw::model::ObjectId> copies;
    fixture.checkUndoable("Duplicate Clip", [&] { copies = session.duplicateClips({clip}); });
    REQUIRE(copies.size() == 1);
    const auto copy = session.getProject().findClip(copies[0]);
    REQUIRE(copy.has_value());
    const auto copied = session.getProject().getTrack(copy->trackIndex).getClip(copy->clipIndex);
    CHECK(copied.getPosition() == 5500);
    CHECK(copied.getOffset() == 500);
    CHECK(copied.getName() == "Verse");

    fixture.checkUndoable("Copy Clip", [&] { session.copyClip(clip, keys, 100); });
    fixture.checkUndoable("Delete Clips", [&] { session.deleteClips({clip, copies[0]}); });
    fixture.checkUndoable("MIDI Input", [&] { session.setTrackMidiInput(keys, {"Keys", 3}); });
    fixture.checkUndoable("MIDI Channel", [&] { session.setTrackMidiChannel(keys, 10); });
}

TEST_CASE("Audio clips are duplicated right after themselves", "[controller][midi]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto track = session.addTrack();
    auto added = trackOf(session, track).addClip(juce::File("/tmp/a.wav"), {100, 10, 400});
    const auto copies = session.duplicateClips({added.getId()});
    REQUIRE(copies.size() == 1);
    const auto copy = trackOf(session, track).getClip(1);
    CHECK(copy.getId() == copies[0]);
    CHECK(copy.getPosition() == 500);
    CHECK(copy.getOffset() == 10);
    CHECK(copy.getAudioFile() == juce::File("/tmp/a.wav"));
    CHECK(session.getUndoDescription() == "Duplicate Clip");
}

TEST_CASE("Every note and controller edit is one undo step", "[controller][midi]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto track = session.addMidiTrack({}, {});
    const auto clip = session.addMidiClip(track, 0, ticksPerSecond * 4);

    std::int32_t first = 0;
    fixture.checkUndoable("Add Note", [&] {
        first = session.addNote(clip, {0, 0, ticksPerSecond / 2, 60, 100});
    });
    const auto second = session.addNote(clip, {0, ticksPerSecond, ticksPerSecond / 2, 64, 80});
    CHECK(first != second);

    const auto edit = [&](const char* name, auto&& change) {
        fixture.checkUndoable(name, [&] {
            auto notes = session.findMidiClip(clip)->getNotes();
            for (auto& note : notes)
                change(note);
            session.setNotes(clip, notes, name);
        });
    };
    edit("Move Notes", [](Note& note) { note.start += ticksPerSecond / 4; });
    edit("Resize Notes", [](Note& note) { note.length *= 2; });
    edit("Velocity", [](Note& note) { note.velocity = 30; });
    edit("Transpose", [](Note& note) { note.key += 12; });
    fixture.checkUndoable("Delete Note", [&] { session.removeNotes(clip, {first}); });

    fixture.checkUndoable("Draw Controller", [&] {
        session.setControllerEvents(clip, ControllerType::controlChange(64),
                                    {{{}, 0, 127}, {{}, ticksPerSecond, 0}}, "Draw Controller");
    });
    fixture.checkUndoable("Draw Bend", [&] {
        session.setControllerEvents(clip, ControllerType::pitchBend(), {{{}, 100, 12000}},
                                    "Draw Bend");
    });
    CHECK(session.findMidiClip(clip)->getControllerEvents().size() == 3);

    // An edit that changes nothing adds no step.
    const auto description = session.getUndoDescription();
    session.setNotes(clip, session.findMidiClip(clip)->getNotes(), "Nothing");
    CHECK(session.getUndoDescription() == description);

    // A drag is one step, however many times it moved the notes.
    const auto before = fixture.snapshot();
    session.beginGesture("Move Notes");
    for (int step = 1; step <= 50; ++step)
    {
        auto notes = session.findMidiClip(clip)->getNotes();
        for (auto& note : notes)
            note.start += 1000;
        session.setNotes(clip, notes, "Move Notes");
    }
    session.endGesture();
    CHECK(session.findMidiClip(clip)->getNotes()[0].start == ticksPerSecond * 5 / 4 + 50000);
    REQUIRE(session.undo());
    CHECK(fixture.snapshot() == before);
}

TEST_CASE("Instruments can only be set on MIDI tracks, effects only in chains",
          "[controller][midi]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto audio = session.addTrack();
    // Unknown types and audio tracks are refused.
    CHECK(session.setInstrument(audio, "synth") == 0);
    const auto midi = session.addMidiTrack({}, "no such instrument");
    CHECK_FALSE(trackOf(session, midi).getInstrument().has_value());
    CHECK(session.setInstrument(midi, "equaliser") == 0);
}
