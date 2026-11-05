#pragma once

#include "daw/engine/midi/MidiEvent.h"

#include <vector>

namespace daw::engine::midi
{

/**
    The events of one block, gathered from several sources (the timeline, devices,
    the preview) and then sorted. Fixed capacity: nothing is allocated after
    prepare().

    Note-offs must never be lost, or a note would hang. So only note-offs may use
    the second half of the buffer: everything else is refused once half of it, less
    room for releasing every held note, is full. As long as note-offs are only sent
    for notes that were accepted (HeldNotes), they always fit.
*/
class EventBuffer
{
public:
    /** Notes a source may hold at once (HeldNotes' capacity). */
    static constexpr int maximumHeldNotes = 256;

    static constexpr int defaultCapacity = 4096;

    /** Not real-time safe. capacity must be more than 2 * maximumHeldNotes. */
    void prepare(int capacity = defaultCapacity);

    /** Audio thread. */
    void clear() noexcept { count = 0; }

    /** Audio thread. False if refused (full; see the class comment). */
    bool add(const Event& event) noexcept;

    /** Audio thread. Takes out the note-on of noteId at offset, if this buffer has
        one; false if not. A note that would end on the sample it starts is not
        played at all: sorted, its note-off would come first (see comesBefore()). */
    bool removeNoteOn(std::int32_t noteId, int offset) noexcept;

    /** Audio thread. Sorts the events by offset, then by orderOf() (stable: events
        of the same order keep the order they were added in). Allocates nothing. */
    void sort() noexcept;

    /** Audio thread. Moves every event by delta samples. */
    void shift(int delta) noexcept;

    [[nodiscard]] EventSpan events() const noexcept
    {
        return {storage.data(), static_cast<std::size_t>(count)};
    }
    [[nodiscard]] int size() const noexcept { return count; }
    [[nodiscard]] bool isEmpty() const noexcept { return count == 0; }
    [[nodiscard]] int getCapacity() const noexcept { return static_cast<int>(storage.size()); }

    /** Whether an event other than a note-off would still be accepted. */
    [[nodiscard]] bool hasRoomForNotes() const noexcept { return count < softLimit; }

private:
    std::vector<Event> storage;
    int count = 0;
    int softLimit = 0;
};

} // namespace daw::engine::midi
