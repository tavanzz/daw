#include "daw/engine/midi/LiveInput.h"

namespace daw::engine::midi
{

double hostSeconds() noexcept
{
    return juce::Time::getMillisecondCounterHiRes() * 0.001;
}

bool parse(const TimedMessage& message, Event& event) noexcept
{
    if (message.size < 2)
        return false;

    const int status = message.bytes[0] & 0xf0;
    event.channel = static_cast<std::uint8_t>(message.bytes[0] & 0x0f);
    event.noteId = -1;
    const int data1 = message.bytes[1] & 0x7f;
    const int data2 = message.size >= 3 ? message.bytes[2] & 0x7f : 0;

    switch (status)
    {
    case 0x90:
        if (message.size < 3)
            return false;
        event.type = data2 > 0 ? EventType::noteOn : EventType::noteOff;
        event.number = static_cast<std::uint8_t>(data1);
        event.value = data2 > 0 ? fromSevenBit(data2) : 0.0f;
        return true;
    case 0x80:
        if (message.size < 3)
            return false;
        event.type = EventType::noteOff;
        event.number = static_cast<std::uint8_t>(data1);
        event.value = fromSevenBit(data2);
        return true;
    case 0xb0:
        if (message.size < 3)
            return false;
        event.type = EventType::controller;
        event.number = static_cast<std::uint8_t>(data1);
        event.value = fromSevenBit(data2);
        return true;
    case 0xe0:
        if (message.size < 3)
            return false;
        event.type = EventType::pitchBend;
        event.number = 0;
        event.value = fromPitchBend(data1 | (data2 << 7));
        return true;
    case 0xd0:
        event.type = EventType::channelPressure;
        event.number = 0;
        event.value = fromSevenBit(data1);
        return true;
    default:
        return false;
    }
}

void LiveNotes::receive(Event event, EventBuffer& events) noexcept
{
    switch (event.type)
    {
    case EventType::noteOn:
        if (!events.hasRoomForNotes())
            return;
        event.noteId = nextNoteId;
        nextNoteId = nextNoteId == std::numeric_limits<std::int32_t>::max() ? firstNoteId
                                                                           : nextNoteId + 1;
        if (held.add(event))
            events.add(event);
        return;

    case EventType::noteOff:
        for (int i = 0; i < held.size(); ++i)
        {
            if (held[i].channel == event.channel && held[i].key == event.number)
            {
                event.noteId = held[i].noteId;
                held.remove(event);
                events.add(event);
                return;
            }
        }
        return;

    case EventType::controller:
    case EventType::pitchBend:
    case EventType::channelPressure:
        if (!events.add(event))
            return;
        for (int i = 0; i < movedCount; ++i)
            if (moved[static_cast<std::size_t>(i)].type == event.type &&
                moved[static_cast<std::size_t>(i)].number == event.number &&
                moved[static_cast<std::size_t>(i)].channel == event.channel)
                return;
        if (movedCount < maximumControllers)
            moved[static_cast<std::size_t>(movedCount++)] = {event.type, event.number, event.channel};
        return;
    }
}

void LiveNotes::releaseAll(int offset, EventBuffer& events) noexcept
{
    held.releaseAll(events, offset);
    for (int i = 0; i < movedCount; ++i)
    {
        const auto& controller = moved[static_cast<std::size_t>(i)];
        events.add({offset, controller.type, controller.channel, controller.number,
                    restValue(controller.type, controller.number), -1});
    }
    movedCount = 0;
}

void LiveNotes::adopt(LiveNotes& previous) noexcept
{
    held = previous.held;
    moved = previous.moved;
    movedCount = previous.movedCount;
    nextNoteId = previous.nextNoteId;
    previous.held.clear();
    previous.movedCount = 0;
}

} // namespace daw::engine::midi
