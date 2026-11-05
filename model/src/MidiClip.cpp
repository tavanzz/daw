#include "daw/model/MidiClip.h"

#include "Identifiers.h"

#include <algorithm>
#include <set>

namespace daw::model
{

namespace
{

// The lists are kept as packed little-endian records in a binary property: one
// value per list, so that an edit replaces it in one step.
constexpr std::size_t noteRecordSize = 4 + 8 + 8 + 1 + 1;
constexpr std::size_t eventRecordSize = 1 + 1 + 8 + 2;

void putInt32(juce::MemoryOutputStream& stream, std::int32_t value)
{
    stream.writeInt(value);
}

juce::MemoryBlock encode(const std::vector<Note>& notes)
{
    juce::MemoryOutputStream stream(notes.size() * noteRecordSize);
    for (const auto& note : notes)
    {
        putInt32(stream, note.id);
        stream.writeInt64(note.start);
        stream.writeInt64(note.length);
        stream.writeByte(static_cast<char>(note.key));
        stream.writeByte(static_cast<char>(note.velocity));
    }
    return stream.getMemoryBlock();
}

std::vector<Note> decodeNotes(const juce::var& value)
{
    std::vector<Note> notes;
    const auto* block = value.getBinaryData();
    if (block == nullptr)
        return notes;

    juce::MemoryInputStream stream(*block, false);
    notes.reserve(block->getSize() / noteRecordSize);
    while (stream.getNumBytesRemaining() >= static_cast<juce::int64>(noteRecordSize))
    {
        Note note;
        note.id = stream.readInt();
        note.start = stream.readInt64();
        note.length = stream.readInt64();
        note.key = static_cast<juce::uint8>(stream.readByte());
        note.velocity = static_cast<juce::uint8>(stream.readByte());
        notes.push_back(note);
    }
    return notes;
}

juce::MemoryBlock encode(const std::vector<ControllerEvent>& events)
{
    juce::MemoryOutputStream stream(events.size() * eventRecordSize);
    for (const auto& event : events)
    {
        stream.writeByte(static_cast<char>(event.type.kind));
        stream.writeByte(static_cast<char>(event.type.number));
        stream.writeInt64(event.position);
        stream.writeShort(static_cast<short>(event.value));
    }
    return stream.getMemoryBlock();
}

std::vector<ControllerEvent> decodeEvents(const juce::var& value)
{
    std::vector<ControllerEvent> events;
    const auto* block = value.getBinaryData();
    if (block == nullptr)
        return events;

    juce::MemoryInputStream stream(*block, false);
    events.reserve(block->getSize() / eventRecordSize);
    while (stream.getNumBytesRemaining() >= static_cast<juce::int64>(eventRecordSize))
    {
        ControllerEvent event;
        const auto kind = static_cast<juce::uint8>(stream.readByte());
        event.type.kind = static_cast<ControllerType::Kind>(std::min<int>(kind, 2));
        event.type.number = static_cast<juce::uint8>(stream.readByte());
        event.position = stream.readInt64();
        event.value = static_cast<juce::uint16>(stream.readShort());
        events.push_back(event);
    }
    return events;
}

} // namespace

int ControllerType::restValue() const noexcept
{
    if (kind == Kind::pitchBend)
        return 8192;
    if (kind == Kind::controlChange && (number == 7 || number == 11))
        return 127;
    return 0;
}

juce::String ControllerType::toString() const
{
    switch (kind)
    {
    case Kind::pitchBend:
        return "pitchBend";
    case Kind::channelPressure:
        return "aftertouch";
    case Kind::controlChange:
        break;
    }
    return "cc:" + juce::String(number);
}

std::optional<ControllerType> ControllerType::fromString(const juce::String& text)
{
    if (text == "pitchBend")
        return pitchBend();
    if (text == "aftertouch")
        return channelPressure();
    if (text.startsWith("cc:"))
    {
        const auto digits = text.fromFirstOccurrenceOf(":", false, false);
        if (digits.isNotEmpty() && digits.containsOnly("0123456789") && digits.length() <= 3)
            if (const int number = digits.getIntValue(); number <= 127)
                return controlChange(number);
    }
    return std::nullopt;
}

void normalise(std::vector<Note>& notes)
{
    std::set<std::int32_t> used;
    std::int32_t largest = 0;
    for (const auto& note : notes)
        largest = std::max(largest, note.id);

    for (auto& note : notes)
    {
        note.start = std::max(Ticks{0}, note.start);
        note.length = std::max(Ticks{1}, note.length);
        note.key = std::clamp(note.key, 0, 127);
        note.velocity = std::clamp(note.velocity, Note::minimumVelocity, Note::maximumVelocity);
        if (note.id <= 0 || !used.insert(note.id).second)
        {
            note.id = ++largest;
            used.insert(note.id);
        }
    }

    std::stable_sort(notes.begin(), notes.end(), [](const Note& a, const Note& b) {
        return a.start != b.start ? a.start < b.start : a.key < b.key;
    });
}

void normalise(std::vector<ControllerEvent>& events)
{
    for (auto& event : events)
    {
        event.type.number = event.type.kind == ControllerType::Kind::controlChange
                                ? std::clamp(event.type.number, 0, 127)
                                : 0;
        event.position = std::max(Ticks{0}, event.position);
        event.value = std::clamp(event.value, 0, event.type.maximumValue());
    }

    std::stable_sort(events.begin(), events.end(),
                     [](const ControllerEvent& a, const ControllerEvent& b) {
                         if (a.type != b.type)
                             return a.type < b.type;
                         return a.position < b.position;
                     });
}

MidiClip::MidiClip(juce::ValueTree clipState)
    : state(std::move(clipState))
{
    jassert(state.hasType(ids::clip));
}

std::vector<Note> MidiClip::getNotes() const
{
    return decodeNotes(state[ids::notes]);
}

void MidiClip::setNotes(std::vector<Note> notes, juce::UndoManager* undoManager)
{
    normalise(notes);
    state.setProperty(ids::notes, encode(notes), undoManager);
}

std::optional<Note> MidiClip::findNote(std::int32_t noteId) const
{
    for (const auto& note : getNotes())
        if (note.id == noteId)
            return note;
    return std::nullopt;
}

std::vector<ControllerEvent> MidiClip::getControllerEvents() const
{
    return decodeEvents(state[ids::controllers]);
}

std::vector<ControllerEvent> MidiClip::getControllerEvents(const ControllerType& type) const
{
    auto events = getControllerEvents();
    events.erase(std::remove_if(events.begin(), events.end(),
                                [&type](const ControllerEvent& event) { return event.type != type; }),
                 events.end());
    return events;
}

std::vector<ControllerType> MidiClip::getControllerTypes() const
{
    std::vector<ControllerType> types;
    for (const auto& event : getControllerEvents())
        if (types.empty() || types.back() != event.type)
            types.push_back(event.type);
    return types;
}

void MidiClip::setControllerEvents(std::vector<ControllerEvent> events,
                                   juce::UndoManager* undoManager)
{
    normalise(events);
    state.setProperty(ids::controllers, encode(events), undoManager);
}

void MidiClip::setControllerEvents(const ControllerType& type, std::vector<ControllerEvent> events,
                                   juce::UndoManager* undoManager)
{
    auto all = getControllerEvents();
    all.erase(std::remove_if(all.begin(), all.end(),
                             [&type](const ControllerEvent& event) { return event.type == type; }),
              all.end());
    for (auto& event : events)
    {
        event.type = type;
        all.push_back(event);
    }
    setControllerEvents(std::move(all), undoManager);
}

} // namespace daw::model
