#include "daw/engine/NotePlayback.h"

#include <algorithm>

namespace daw::engine
{

namespace
{

// The first note starting at or after position.
std::vector<ArrangementNote>::const_iterator firstNoteFrom(const std::vector<ArrangementNote>& notes,
                                                           juce::int64 position) noexcept
{
    return std::lower_bound(notes.begin(), notes.end(), position,
                            [](const ArrangementNote& note, juce::int64 p) { return note.start < p; });
}

} // namespace

void NotePlayback::play(const ArrangementTrack& track, juce::int64 start, int length,
                        midi::EventBuffer& events) noexcept
{
    if (length <= 0)
        return;

    const bool jumped = start != expected;
    if (jumped)
        endAll(0, events);

    if (jumped || chasePending)
    {
        chase(track, start, jumped, events);
        chasePending = false;
    }

    const juce::int64 end = start + length;

    for (const auto& lane : track.controllers)
    {
        auto step = std::lower_bound(lane.steps.begin(), lane.steps.end(), start,
                                     [](const ControllerStep& s, juce::int64 p) {
                                         return s.position < p;
                                     });
        for (; step != lane.steps.end() && step->position < end; ++step)
            send(lane.type, lane.number, lane.channel, step->value,
                 static_cast<int>(step->position - start), events);
    }

    for (auto note = firstNoteFrom(track.notes, start);
         note != track.notes.end() && note->start < end; ++note)
    {
        const int offset = static_cast<int>(note->start - start);

        // What ends by now ends first; a note still sounding on this key is ended
        // where it is struck again.
        endNotesBefore(note->start + 1, start, length, events);
        for (int i = count - 1; i >= 0; --i)
            if (active[static_cast<std::size_t>(i)].key == note->key &&
                active[static_cast<std::size_t>(i)].channel == note->channel)
                endNote(i, offset, events);

        if (count >= capacity || !events.hasRoomForNotes())
            continue;

        const std::int32_t noteId = nextNoteId;
        nextNoteId = nextNoteId == std::numeric_limits<std::int32_t>::max() ? 0 : nextNoteId + 1;
        events.add(midi::noteOn(offset, note->key, note->velocity, note->channel, noteId));
        active[static_cast<std::size_t>(count++)] = {note->identity, note->start, note->end,
                                                     note->key,      note->channel, noteId};
    }

    endNotesBefore(end, start, length, events);
    expected = end;
}

void NotePlayback::stop(int offset, midi::EventBuffer& events) noexcept
{
    endAll(offset, events);

    for (int i = 0; i < sentCount; ++i)
    {
        const auto& value = sent[static_cast<std::size_t>(i)];
        midi::Event event{offset, value.type, value.channel, value.number,
                          midi::restValue(value.type, value.number), -1};
        events.add(event);
    }
    sentCount = 0;
    expected = -1;
    chasePending = true;
}

void NotePlayback::adopt(NotePlayback& previous, const ArrangementTrack& track) noexcept
{
    active = previous.active;
    count = previous.count;
    sent = previous.sent;
    sentCount = previous.sentCount;
    expected = previous.expected;
    nextNoteId = previous.nextNoteId;
    chasePending = true;

    previous.count = 0;
    previous.sentCount = 0;

    for (int i = 0; i < count; ++i)
    {
        auto& note = active[static_cast<std::size_t>(i)];
        juce::int64 newEnd = endsNow;
        for (auto match = firstNoteFrom(track.notes, note.start);
             match != track.notes.end() && match->start == note.start; ++match)
        {
            if (note.identity != 0 && match->identity == note.identity &&
                match->key == note.key && match->channel == note.channel)
            {
                newEnd = match->end;
                break;
            }
        }
        note.end = newEnd;
    }
}

void NotePlayback::endNotesBefore(juce::int64 position, juce::int64 start, int length,
                                  midi::EventBuffer& events) noexcept
{
    for (int i = count - 1; i >= 0; --i)
    {
        const juce::int64 noteEnd = active[static_cast<std::size_t>(i)].end;
        if (noteEnd < position)
        {
            const juce::int64 offset = noteEnd == endsNow ? 0 : noteEnd - start;
            endNote(i, static_cast<int>(std::clamp<juce::int64>(offset, 0, length - 1)), events);
        }
    }
}

void NotePlayback::endNote(int index, int offset, midi::EventBuffer& events) noexcept
{
    const auto note = active[static_cast<std::size_t>(index)];
    events.add(midi::noteOff(offset, note.key, note.channel, note.noteId));
    for (int i = index + 1; i < count; ++i)
        active[static_cast<std::size_t>(i - 1)] = active[static_cast<std::size_t>(i)];
    --count;
}

void NotePlayback::endAll(int offset, midi::EventBuffer& events) noexcept
{
    for (int i = 0; i < count; ++i)
    {
        const auto& note = active[static_cast<std::size_t>(i)];
        events.add(midi::noteOff(offset, note.key, note.channel, note.noteId));
    }
    count = 0;
}

void NotePlayback::chase(const ArrangementTrack& track, juce::int64 start, bool fromRest,
                         midi::EventBuffer& events) noexcept
{
    // After a jump, back to rest first where something was moved, so that a sustain
    // pedal held across the jump lets the ended notes go.
    if (fromRest)
    {
        for (int i = 0; i < sentCount; ++i)
        {
            const auto& value = sent[static_cast<std::size_t>(i)];
            midi::Event event{0, value.type, value.channel, value.number,
                              midi::restValue(value.type, value.number), -1};
            events.add(event);
        }
        sentCount = 0;
    }

    // Controllers no lane moves any more go back to rest.
    for (int i = sentCount - 1; i >= 0; --i)
    {
        const auto value = sent[static_cast<std::size_t>(i)];
        const bool hasLane = std::any_of(track.controllers.begin(), track.controllers.end(),
                                         [&](const ControllerLane& lane) {
                                             return lane.type == value.type &&
                                                    lane.channel == value.channel &&
                                                    (lane.type != midi::EventType::controller ||
                                                     lane.number == value.number);
                                         });
        if (!hasLane)
            send(value.type, value.number, value.channel,
                 midi::restValue(value.type, value.number), 0, events);
    }

    // Then each lane's value here, where it differs from what was sent.
    for (const auto& lane : track.controllers)
    {
        const float value = lane.valueBefore(start);
        const int index = findSent(lane.type, lane.number, lane.channel);
        const float current = index >= 0 ? sent[static_cast<std::size_t>(index)].value
                                         : midi::restValue(lane.type, lane.number);
        if (!juce::exactlyEqual(value, current))
            send(lane.type, lane.number, lane.channel, value, 0, events);
    }
}

void NotePlayback::send(midi::EventType type, int number, int channel, float value, int offset,
                        midi::EventBuffer& events) noexcept
{
    midi::Event event{offset, type, static_cast<std::uint8_t>(channel),
                      static_cast<std::uint8_t>(number), value, -1};
    if (!events.add(event))
        return;

    const int index = findSent(type, number, channel);
    const bool atRest = juce::exactlyEqual(value, midi::restValue(type, number));
    if (index >= 0)
    {
        if (atRest)
        {
            sent[static_cast<std::size_t>(index)] = sent[static_cast<std::size_t>(sentCount - 1)];
            --sentCount;
        }
        else
        {
            sent[static_cast<std::size_t>(index)].value = value;
        }
    }
    else if (!atRest && sentCount < maximumControllers)
    {
        sent[static_cast<std::size_t>(sentCount++)] = {type, static_cast<std::uint8_t>(number),
                                                       static_cast<std::uint8_t>(channel), value};
    }
}

int NotePlayback::findSent(midi::EventType type, int number, int channel) const noexcept
{
    for (int i = 0; i < sentCount; ++i)
    {
        const auto& value = sent[static_cast<std::size_t>(i)];
        if (value.type == type && value.channel == channel &&
            (type != midi::EventType::controller || value.number == number))
            return i;
    }
    return -1;
}

} // namespace daw::engine
