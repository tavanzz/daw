#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/midi/EventBuffer.h"

#include <array>
#include <cstdint>
#include <limits>

namespace daw::engine
{

/**
    What a MIDI track's timeline sends its instrument, block by block: the note-ons,
    note-offs and controller changes of each stretch, at their samples.

    It keeps the notes it started, so that each gets its note-off whatever happens:
    - a stretch that does not continue the last one (a seek, the loop's wrap,
      playback starting) first ends every note at its first sample, and then sends
      each controller's value there (chase); notes that started earlier are not
      restarted;
    - stop() ends every note (the transport stopping or fading out, a track going
      away) and puts the controllers back at rest, so a held sustain pedal cannot
      keep the released notes sounding;
    - a key struck again while it sounds is ended first, so every receiver gets
      well-formed MIDI 1.0 pairs;
    - adopt() takes over from the playback of the previous arrangement: notes the
      new one still has (same identity and start) go on to their new end, the
      others end at once; controllers are chased again.

    Its result depends only on the timeline positions it is given, never on how they
    are cut into blocks. Audio thread (the thread that renders); fixed size.
*/
class NotePlayback
{
public:
    static constexpr int capacity = midi::HeldNotes::capacity;
    static constexpr int maximumControllers = 64;

    /** Adds what [start, start + length) of track's timeline plays to events, at
        offsets from start. */
    void play(const ArrangementTrack& track, juce::int64 start, int length,
              midi::EventBuffer& events) noexcept;

    /** Ends every note at offset and returns the controllers to rest. */
    void stop(int offset, midi::EventBuffer& events) noexcept;

    /** Panic: forgets every note and controller sent (the instrument was reset);
        the next stretch chases the controllers again. */
    void forget() noexcept;

    /** Takes over previous's notes and controllers for track (see the class comment). */
    void adopt(NotePlayback& previous, const ArrangementTrack& track) noexcept;

    /** How many notes are sounding. */
    [[nodiscard]] int getNumActiveNotes() const noexcept { return count; }

private:
    static constexpr juce::int64 endsNow = std::numeric_limits<juce::int64>::min();

    struct ActiveNote
    {
        std::uint64_t identity = 0;
        juce::int64 start = 0;
        juce::int64 end = 0;
        std::uint8_t key = 0;
        std::uint8_t channel = 0;
        std::int32_t noteId = -1;
    };

    struct SentValue
    {
        midi::EventType type = midi::EventType::controller;
        std::uint8_t number = 0;
        std::uint8_t channel = 0;
        float value = 0.0f;
    };

    void endNotesBefore(juce::int64 position, juce::int64 start, int length,
                        midi::EventBuffer& events) noexcept;
    void endNote(int index, int offset, midi::EventBuffer& events) noexcept;
    void endAll(int offset, midi::EventBuffer& events) noexcept;
    void chase(const ArrangementTrack& track, juce::int64 start, bool fromRest,
               midi::EventBuffer& events) noexcept;
    void send(midi::EventType type, int number, int channel, float value, int offset,
              midi::EventBuffer& events) noexcept;
    [[nodiscard]] int findSent(midi::EventType type, int number, int channel) const noexcept;

    std::array<ActiveNote, capacity> active{};
    int count = 0;

    std::array<SentValue, maximumControllers> sent{}; // controllers away from rest
    int sentCount = 0;

    juce::int64 expected = -1; // where the next stretch continues; -1 after a stop
    bool chasePending = true;
    std::int32_t nextNoteId = 0;
};

} // namespace daw::engine
