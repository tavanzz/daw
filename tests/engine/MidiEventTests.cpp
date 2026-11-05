#include "daw/engine/midi/EventBuffer.h"
#include "daw/engine/midi/HeldNotes.h"
#include "daw/engine/midi/MidiEvent.h"

#include "support/RealtimeCheck.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

namespace midi = daw::engine::midi;

TEST_CASE("MIDI 1.0 values convert exactly to normalised values and back", "[engine][midi]")
{
    for (int value = 0; value <= 127; ++value)
    {
        const float normalised = midi::fromSevenBit(value);
        REQUIRE(normalised >= 0.0f);
        REQUIRE(normalised <= 1.0f);
        REQUIRE(midi::toSevenBit(normalised) == value);
    }
    REQUIRE(midi::fromSevenBit(127) == 1.0f);

    for (int value = 0; value <= 16383; ++value)
        REQUIRE(midi::toPitchBend(midi::fromPitchBend(value)) == value);
    REQUIRE(midi::fromPitchBend(8192) == 0.0f);
    REQUIRE(midi::fromPitchBend(0) == -1.0f);
    REQUIRE(midi::toPitchBend(1.0f) == 16383);
}

TEST_CASE("Event buffers sort by sample, then note-offs, controllers and note-ons",
          "[engine][midi]")
{
    midi::EventBuffer buffer;
    buffer.prepare(1024);

    buffer.add(midi::noteOn(5, 60, 0.5f));
    buffer.add(midi::noteOff(5, 60));
    buffer.add(midi::controller(5, 64, 1.0f));
    buffer.add(midi::noteOn(2, 61, 0.5f));
    buffer.add(midi::noteOn(5, 62, 0.5f));
    buffer.add(midi::pitchBend(0, 0.25f));

    std::size_t heap = daw::test::heapOperationsDuring([&] { buffer.sort(); });
    REQUIRE(heap == 0);

    const auto events = buffer.events();
    REQUIRE(events.size() == 6);
    REQUIRE(events[0].type == midi::EventType::pitchBend);
    REQUIRE(events[1].offset == 2);
    REQUIRE(events[2].type == midi::EventType::noteOff);
    REQUIRE(events[3].type == midi::EventType::controller);
    // Note-ons at one sample keep the order they were added in.
    REQUIRE(events[4].number == 60);
    REQUIRE(events[5].number == 62);

    buffer.shift(-2);
    REQUIRE(buffer.events()[1].offset == 0);
}

TEST_CASE("Event buffers always keep room for note-offs", "[engine][midi]")
{
    midi::EventBuffer buffer;
    buffer.prepare(1024);

    int notesAccepted = 0;
    while (buffer.add(midi::noteOn(0, notesAccepted % 128, 0.5f)))
        ++notesAccepted;
    REQUIRE(notesAccepted == 1024 / 2 - midi::EventBuffer::maximumHeldNotes);
    REQUIRE_FALSE(buffer.hasRoomForNotes());
    REQUIRE_FALSE(buffer.add(midi::controller(0, 1, 0.5f)));

    // Every accepted note, and every note a source may already hold, can still end.
    for (int i = 0; i < notesAccepted + midi::EventBuffer::maximumHeldNotes; ++i)
        REQUIRE(buffer.add(midi::noteOff(1, i % 128)));
}

TEST_CASE("Held notes match note-offs by id, otherwise by key, and release them all",
          "[engine][midi]")
{
    midi::HeldNotes held;
    REQUIRE(held.add(midi::noteOn(0, 60, 0.5f, 0, 7)));
    REQUIRE(held.add(midi::noteOn(0, 60, 0.5f, 0, 8)));
    REQUIRE(held.add(midi::noteOn(0, 64, 0.5f, 3)));
    REQUIRE(held.isHeld(0, 60));
    REQUIRE(held.isHeld(3, 64));
    REQUIRE_FALSE(held.isHeld(0, 64));

    REQUIRE(held.remove(midi::noteOff(0, 60, 0, 8)));
    REQUIRE(held.size() == 2);
    REQUIRE(held[0].noteId == 7);
    REQUIRE_FALSE(held.remove(midi::noteOff(0, 61)));
    REQUIRE_FALSE(held.remove(midi::noteOff(0, 64))); // another channel

    midi::EventBuffer buffer;
    buffer.prepare(1024);
    held.releaseAll(buffer, 12);
    REQUIRE(held.isEmpty());
    REQUIRE(buffer.size() == 2);
    REQUIRE(buffer.events()[0] == midi::noteOff(12, 60, 0, 7));
    REQUIRE(buffer.events()[1] == midi::noteOff(12, 64, 3));

    for (int i = 0; i < midi::HeldNotes::capacity; ++i)
        REQUIRE(held.add(midi::noteOn(0, i % 128, 0.5f)));
    REQUIRE_FALSE(held.add(midi::noteOn(0, 1, 0.5f)));
}
