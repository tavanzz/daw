#pragma once

#include "daw/engine/midi/EventBuffer.h"

#include <array>
#include <cstdint>

namespace daw::engine::midi
{

/**
    The notes a source has started and not yet ended, so that each can be ended
    whatever happens (bypass, a device unplugged, the transport jumping). A
    note-off matches the held note with its note id, or, without one, the oldest
    held note on its channel and key.

    Fixed size, real-time safe; one thread only.
*/
class HeldNotes
{
public:
    static constexpr int capacity = EventBuffer::maximumHeldNotes;

    struct Note
    {
        std::uint8_t channel = 0;
        std::uint8_t key = 0;
        std::int32_t noteId = -1;
    };

    /** False if the table is full: the note must not be sent. */
    bool add(const Event& noteOn) noexcept;

    /** False if no held note matches (the note-off must not be sent). */
    bool remove(const Event& noteOff) noexcept;

    /** Whether a note on this channel and key is held. */
    [[nodiscard]] bool isHeld(int channel, int key) const noexcept;

    /** Adds a note-off at offset for every held note to events (oldest first) and
        forgets them. */
    void releaseAll(EventBuffer& events, int offset) noexcept;

    void clear() noexcept { count = 0; }

    [[nodiscard]] int size() const noexcept { return count; }
    [[nodiscard]] bool isEmpty() const noexcept { return count == 0; }
    [[nodiscard]] const Note& operator[](int index) const noexcept
    {
        return notes[static_cast<std::size_t>(index)];
    }

private:
    void removeAt(int index) noexcept;

    std::array<Note, capacity> notes{};
    int count = 0;
};

/** Whether event changes which notes sound (a note-on or a note-off). */
[[nodiscard]] constexpr bool isNote(const Event& event) noexcept
{
    return event.type == EventType::noteOn || event.type == EventType::noteOff;
}

} // namespace daw::engine::midi
