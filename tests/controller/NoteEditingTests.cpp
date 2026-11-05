#include "daw/controller/NoteEditing.h"
#include "daw/controller/PianoRollViewport.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <cmath>
#include <vector>

using daw::controller::ClipFrame;
using daw::controller::NoteIds;
using daw::controller::NotePart;
using daw::controller::PianoRollViewport;
using daw::controller::QuantiseSettings;
using daw::controller::Snapper;
using daw::model::ControllerEvent;
using daw::model::ControllerType;
using daw::model::Grid;
using daw::model::GridDivision;
using daw::model::Note;
using daw::model::TempoMap;
using daw::model::Ticks;

namespace
{

const TempoMap tempo120(120.0, {4, 4});
constexpr Ticks quarter = 141'120'000; // at 120 BPM
constexpr Ticks sixteenth = quarter / 4;

// A clip at 1000 showing its content from 500 on: content tick c is at 500 + c.
const ClipFrame frame{500, 1000, 1000 + 16 * quarter};

Note noteAtTimeline(std::int32_t id, Ticks start, Ticks length, int key = 60)
{
    return {id, frame.toContent(start), length, key, 100};
}

Ticks rounded(double value)
{
    return static_cast<Ticks>(std::llround(value));
}

} // namespace

TEST_CASE("Quantising moves note starts towards the grid by the strength",
          "[controller][midi][quantise]")
{
    const double strength = GENERATE(0.0, 0.25, 0.5, 0.75, 1.0);
    const std::vector<Note> notes{noteAtTimeline(1, rounded(1.3 * sixteenth), sixteenth / 2),
                                  noteAtTimeline(2, rounded(6.8 * sixteenth), sixteenth * 3),
                                  noteAtTimeline(3, 4 * sixteenth, sixteenth, 64)};

    const auto result =
        daw::controller::quantised(notes, {}, frame, tempo120, {GridDivision::sixteenth, strength, false});
    REQUIRE(result.size() == 3);

    const auto expectStart = [&](std::size_t index, Ticks onGrid) {
        const auto from = frame.toTimeline(notes[index].start);
        CAPTURE(index, strength);
        CHECK(frame.toTimeline(result[index].start) ==
              from + rounded(static_cast<double>(onGrid - from) * strength));
        CHECK(result[index].length == notes[index].length);
    };
    expectStart(0, sixteenth);
    expectStart(1, 7 * sixteenth);
    expectStart(2, 4 * sixteenth); // already there
}

TEST_CASE("Quantising follows the chosen grid, triplets too, on the timeline",
          "[controller][midi][quantise]")
{
    struct Case
    {
        GridDivision division;
        Ticks step;
    };
    const auto chosen = GENERATE(Case{GridDivision::quarter, quarter},
                                 Case{GridDivision::eighth, quarter / 2},
                                 Case{GridDivision::eighthTriplet, quarter / 3},
                                 Case{GridDivision::sixteenthTriplet, quarter / 6},
                                 Case{GridDivision::thirtySecond, quarter / 8},
                                 Case{GridDivision::sixtyFourth, quarter / 16});

    // Just past the 5th line, just before the 9th: to the nearest one, on the
    // timeline (not in the clip's content ticks).
    const std::vector<Note> notes{noteAtTimeline(1, 5 * chosen.step + chosen.step / 3, 100),
                                  noteAtTimeline(2, 9 * chosen.step - chosen.step / 4, 100)};
    const auto result =
        daw::controller::quantised(notes, {}, frame, tempo120, {chosen.division, 1.0, false});
    CHECK(frame.toTimeline(result[0].start) == 5 * chosen.step);
    CHECK(frame.toTimeline(result[1].start) == 9 * chosen.step);
}

TEST_CASE("Quantising ends too keeps every note at least one step long", "[controller][midi][quantise]")
{
    const std::vector<Note> notes{noteAtTimeline(1, rounded(1.2 * sixteenth), rounded(1.5 * sixteenth)),
                                  noteAtTimeline(2, rounded(4.1 * sixteenth), rounded(0.2 * sixteenth)),
                                  noteAtTimeline(3, rounded(8.4 * sixteenth), sixteenth)};

    // Only notes 1 and 2 are selected.
    const auto result = daw::controller::quantised(notes, {1, 2}, frame, tempo120,
                                                   {GridDivision::sixteenth, 1.0, true});
    CHECK(frame.toTimeline(result[0].start) == sixteenth);
    CHECK(frame.toTimeline(result[0].getEnd()) == 3 * sixteenth); // 2.7 -> 3
    CHECK(frame.toTimeline(result[1].start) == 4 * sixteenth);
    CHECK(result[1].length == sixteenth); // 4.1-4.3 collapses: one step
    CHECK(result[2] == notes[2]);
}

TEST_CASE("Transposing moves the selected notes, never past the keyboard", "[controller][midi]")
{
    const std::vector<Note> notes{{1, 0, 10, 60, 100}, {2, 0, 10, 120, 100}, {3, 0, 10, 5, 100}};
    const auto up = daw::controller::transposed(notes, {1, 3}, 12);
    REQUIRE(up.has_value());
    CHECK((*up)[0].key == 72);
    CHECK((*up)[1].key == 120);
    CHECK((*up)[2].key == 17);
    CHECK_FALSE(daw::controller::transposed(notes, {}, 12).has_value());   // 120 + 12
    CHECK_FALSE(daw::controller::transposed(notes, {3}, -6).has_value());  // 5 - 6
    CHECK(daw::controller::transposed(notes, {3}, -5).has_value());
}

TEST_CASE("Dragged notes follow the grabbed one, snapped, and can be copied", "[controller][midi]")
{
    const Snapper snap(Grid(tempo120, GridDivision::sixteenth));
    const std::vector<Note> notes{noteAtTimeline(1, sixteenth, sixteenth, 60),
                                  noteAtTimeline(2, 3 * sixteenth, sixteenth, 64),
                                  noteAtTimeline(3, 8 * sixteenth, sixteenth, 67)};

    // Grabbed by note 2, moved by 1.6 steps: it lands on step 5, note 1 follows.
    auto moved = daw::controller::movedNotes(notes, {1, 2}, 2, rounded(1.6 * sixteenth), 3, frame,
                                             snap, false);
    REQUIRE(moved.notes.size() == 3);
    CHECK(frame.toTimeline(moved.notes[0].start) == 3 * sixteenth);
    CHECK(frame.toTimeline(moved.notes[1].start) == 5 * sixteenth);
    CHECK(moved.notes[0].key == 63);
    CHECK(moved.notes[2] == notes[2]);
    CHECK(moved.selection == NoteIds{1, 2});

    // Never before the content's start, never off the keyboard.
    moved = daw::controller::movedNotes(notes, {1}, 1, -100 * quarter, 200, frame, snap, false);
    CHECK(moved.notes[0].start == 0);
    CHECK(moved.notes[0].key == 127);

    // A copy leaves the originals and selects the copies.
    moved = daw::controller::movedNotes(notes, {3}, 3, quarter, 0, frame, snap, true);
    REQUIRE(moved.notes.size() == 4);
    CHECK(moved.notes[2] == notes[2]);
    CHECK(moved.notes[3].id == 4);
    CHECK(frame.toTimeline(moved.notes[3].start) == 12 * sixteenth);
    CHECK(moved.selection == NoteIds{4});
}

TEST_CASE("Note edges snap and notes keep a length", "[controller][midi]")
{
    const Snapper snap(Grid(tempo120, GridDivision::sixteenth));
    const std::vector<Note> notes{noteAtTimeline(1, sixteenth, sixteenth),
                                  noteAtTimeline(2, 4 * sixteenth, 2 * sixteenth)};

    auto ends = daw::controller::resizedEnds(notes, {1, 2}, 1, rounded(1.4 * sixteenth), frame, snap);
    CHECK(frame.toTimeline(ends[0].getEnd()) == 3 * sixteenth);
    CHECK(ends[1].length == 3 * sixteenth);

    ends = daw::controller::resizedEnds(notes, {1}, 1, -quarter, frame, snap);
    CHECK(ends[0].length == sixteenth); // one grid step at least
    ends = daw::controller::resizedEnds(notes, {1}, 1, -quarter, frame, Snapper{});
    CHECK(ends[0].length == 1);

    auto starts = daw::controller::resizedStarts(notes, {2}, 2, -rounded(1.6 * sixteenth), frame, snap);
    CHECK(frame.toTimeline(starts[1].start) == 2 * sixteenth);
    CHECK(starts[1].getEnd() == notes[1].getEnd());
    starts = daw::controller::resizedStarts(notes, {2}, 2, quarter * 4, frame, snap);
    CHECK(starts[1].length == 1);
}

TEST_CASE("Velocities are set, drawn along a line, and notes found in a rectangle",
          "[controller][midi]")
{
    std::vector<Note> notes;
    for (int i = 0; i < 5; ++i)
        notes.push_back(noteAtTimeline(i + 1, i * quarter, quarter / 2, 60 + i));

    const auto set = daw::controller::withVelocity(notes, {2, 3}, 200);
    CHECK(set[1].velocity == 127);
    CHECK(set[0].velocity == 100);

    const auto drawn = daw::controller::velocitiesAlong(notes, {}, frame, quarter, 20, 3 * quarter, 120);
    CHECK(drawn[0].velocity == 100);
    CHECK(drawn[1].velocity == 20);
    CHECK(drawn[2].velocity == 70);
    CHECK(drawn[3].velocity == 120);
    CHECK(drawn[4].velocity == 100);

    CHECK(daw::controller::notesIn(notes, frame, quarter / 4, 2 * quarter + 1, 0, 127) ==
          NoteIds{1, 2, 3});
    CHECK(daw::controller::notesIn(notes, frame, 0, 10 * quarter, 62, 63) == NoteIds{3, 4});

    const Snapper snap(Grid(tempo120, GridDivision::sixteenth));
    const auto added = daw::controller::newNoteAt(rounded(2.9 * sixteenth), 70, sixteenth, 90, frame, snap);
    CHECK(frame.toTimeline(added.start) == 2 * sixteenth);
    CHECK(added.key == 70);
    CHECK(added.velocity == 90);
}

TEST_CASE("Hit tests find the note and which edge is under the pointer", "[controller][midi]")
{
    const std::vector<Note> notes{noteAtTimeline(1, 0, quarter, 60),
                                  noteAtTimeline(2, quarter / 2, quarter, 60),
                                  noteAtTimeline(3, 0, 150, 62)};
    const Ticks edge = 100;

    auto hit = daw::controller::hitNote(notes, frame, quarter / 4, 60, edge);
    CHECK(hit.id == 1);
    CHECK(hit.part == NotePart::body);
    hit = daw::controller::hitNote(notes, frame, quarter * 3 / 4, 60, edge);
    CHECK(hit.id == 2); // the one starting last
    hit = daw::controller::hitNote(notes, frame, quarter / 2 + 50, 60, edge);
    CHECK(hit.part == NotePart::start);
    hit = daw::controller::hitNote(notes, frame, quarter * 3 / 2 - 1, 60, edge);
    CHECK(hit.part == NotePart::end);
    hit = daw::controller::hitNote(notes, frame, 50, 62, edge);
    CHECK(hit.part == NotePart::end); // short: the end edge wins
    CHECK(daw::controller::hitNote(notes, frame, quarter * 2, 60, edge).part == NotePart::none);
}

TEST_CASE("Controller lines replace the events they cross", "[controller][midi]")
{
    const auto modWheel = ControllerType::controlChange(1);
    const std::vector<ControllerEvent> events{{modWheel, frame.toContent(0), 10},
                                              {modWheel, frame.toContent(quarter), 50},
                                              {modWheel, frame.toContent(4 * quarter), 90},
                                              {ControllerType::pitchBend(), frame.toContent(quarter), 9000}};

    const auto drawn = daw::controller::eventsAlongLine(events, modWheel, frame, quarter / 2, 0,
                                                        3 * quarter, 100, quarter / 4);
    // Kept: 0 (10) and 4q (90) and the bend; the line: every quarter/4 from q/2 to 3q.
    std::vector<ControllerEvent> wheel;
    for (const auto& event : drawn)
        if (event.type == modWheel)
            wheel.push_back(event);
    REQUIRE(wheel.size() == 13);
    CHECK(wheel.front().value == 10);
    CHECK(wheel[1].value == 0);
    CHECK(frame.toTimeline(wheel[1].position) == quarter / 2);
    CHECK(wheel[11].value == 100);
    CHECK(frame.toTimeline(wheel[11].position) == 3 * quarter);
    CHECK(wheel.back().value == 90);
    CHECK(drawn.size() == 14);

    CHECK(daw::controller::controllerValueAt(drawn, modWheel, frame, quarter / 4) == 10);
    CHECK(daw::controller::controllerValueAt(drawn, ControllerType::pitchBend(), frame, 0) == 8192);
    CHECK(daw::controller::controllerValueAt(drawn, ControllerType::pitchBend(), frame, quarter) == 9000);
    CHECK(daw::controller::hitControllerEvent(events, frame, quarter + 10, 48, 100, 5) == 1);
    CHECK(daw::controller::hitControllerEvent(events, frame, quarter + 1000, 48, 100, 5) == -1);
}

TEST_CASE("The piano roll viewport maps keys to rows and keeps them in range",
          "[controller][midi]")
{
    PianoRollViewport viewport;
    viewport.setHeight(240);
    viewport.setKeyHeight(10.0);
    viewport.setTop(72.0);

    CHECK(viewport.keyToY(71) == 0.0);
    CHECK(viewport.keyToY(60) == 110.0);
    CHECK(viewport.yToKey(0.0) == 71);
    CHECK(viewport.yToKey(115.0) == 60);
    CHECK(viewport.yToKey(10000.0) == 0);

    viewport.setTop(500.0);
    CHECK(viewport.getTop() == 128.0);
    viewport.setTop(-5.0);
    CHECK(viewport.getTop() == 24.0); // the bottom key stays at the bottom edge

    viewport.setTop(72.0);
    viewport.zoomAround(2.0, 115.0); // key 60 under the pointer stays there
    CHECK(viewport.getKeyHeight() == 20.0);
    CHECK(viewport.yToKey(115.0) == 60);

    viewport.showKeys(48, 52);
    CHECK(viewport.yToKey(viewport.getHeight() / 2.0) == 50);
}
