#include "daw/engine/midi/HeldNotes.h"

namespace daw::engine::midi
{

bool HeldNotes::add(const Event& noteOn) noexcept
{
    if (count >= capacity)
        return false;
    notes[static_cast<std::size_t>(count++)] = {noteOn.channel, noteOn.number, noteOn.noteId};
    return true;
}

bool HeldNotes::remove(const Event& noteOff) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const auto& note = notes[static_cast<std::size_t>(i)];
        const bool matches = noteOff.noteId >= 0 && note.noteId >= 0
                                 ? note.noteId == noteOff.noteId
                                 : note.channel == noteOff.channel && note.key == noteOff.number;
        if (matches)
        {
            removeAt(i);
            return true;
        }
    }
    return false;
}

bool HeldNotes::isHeld(int channel, int key) const noexcept
{
    for (int i = 0; i < count; ++i)
        if (notes[static_cast<std::size_t>(i)].channel == channel &&
            notes[static_cast<std::size_t>(i)].key == key)
            return true;
    return false;
}

void HeldNotes::releaseAll(EventBuffer& events, int offset) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        // A note started on this very sample is not played at all: sorted, its
        // note-off would come first.
        const auto& note = notes[static_cast<std::size_t>(i)];
        if (!events.removeNoteOn(note.noteId, offset))
            events.add(noteOff(offset, note.key, note.channel, note.noteId));
    }
    count = 0;
}

void HeldNotes::removeAt(int index) noexcept
{
    // Keeps the order (oldest first): matching a note-off without an id takes the
    // oldest note on its key.
    for (int i = index + 1; i < count; ++i)
        notes[static_cast<std::size_t>(i - 1)] = notes[static_cast<std::size_t>(i)];
    --count;
}

} // namespace daw::engine::midi
