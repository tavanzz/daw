#include "daw/engine/midi/EventBuffer.h"

namespace daw::engine::midi
{

void EventBuffer::prepare(int capacity)
{
    jassert(capacity > 2 * maximumHeldNotes);
    storage.assign(static_cast<std::size_t>(capacity), Event{});
    softLimit = capacity / 2 - maximumHeldNotes;
    count = 0;
}

bool EventBuffer::add(const Event& event) noexcept
{
    const int limit = event.type == EventType::noteOff ? static_cast<int>(storage.size()) : softLimit;
    if (count >= limit)
        return false;
    storage[static_cast<std::size_t>(count++)] = event;
    return true;
}

bool EventBuffer::removeNoteOn(std::int32_t noteId, int offset) noexcept
{
    for (int i = count - 1; i >= 0; --i)
    {
        const auto& event = storage[static_cast<std::size_t>(i)];
        if (event.type == EventType::noteOn && event.noteId == noteId && event.offset == offset)
        {
            for (int j = i + 1; j < count; ++j)
                storage[static_cast<std::size_t>(j - 1)] = storage[static_cast<std::size_t>(j)];
            --count;
            return true;
        }
    }
    return false;
}

void EventBuffer::sort() noexcept
{
    // Insertion sort: stable, allocates nothing (std::stable_sort may), and the
    // events come in nearly sorted (each source adds them in order).
    for (int i = 1; i < count; ++i)
    {
        const Event event = storage[static_cast<std::size_t>(i)];
        int j = i;
        while (j > 0 && comesBefore(event, storage[static_cast<std::size_t>(j - 1)]))
        {
            storage[static_cast<std::size_t>(j)] = storage[static_cast<std::size_t>(j - 1)];
            --j;
        }
        storage[static_cast<std::size_t>(j)] = event;
    }
}

void EventBuffer::shift(int delta) noexcept
{
    for (int i = 0; i < count; ++i)
        storage[static_cast<std::size_t>(i)].offset += delta;
}

} // namespace daw::engine::midi
