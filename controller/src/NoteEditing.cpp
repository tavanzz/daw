#include "daw/controller/NoteEditing.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace daw::controller
{

namespace
{

bool affects(const NoteIds& selected, const model::Note& note)
{
    return selected.empty() || selected.contains(note.id);
}

const model::Note* findNote(const std::vector<model::Note>& notes, std::int32_t id)
{
    for (const auto& note : notes)
        if (note.id == id)
            return &note;
    return nullptr;
}

model::Ticks moveBy(model::Ticks position, double fraction, model::Ticks target)
{
    return position + static_cast<model::Ticks>(
                          std::llround(static_cast<double>(target - position) * fraction));
}

} // namespace

ClipFrame ClipFrame::of(const model::Clip& clip)
{
    const auto position = clip.getPosition();
    return {position - clip.getOffset(), position,
            position + clip.getLength().value_or(model::Clip::minimumLength)};
}

std::vector<model::Note> quantised(std::vector<model::Note> notes, const NoteIds& selected,
                                   const ClipFrame& frame, const model::TempoMap& tempoMap,
                                   const QuantiseSettings& settings)
{
    const auto division = settings.division == model::GridDivision::automatic
                              ? model::GridDivision::sixteenth
                              : settings.division;
    const model::Grid grid(tempoMap, division);
    const double strength = std::clamp(settings.strength, 0.0, 1.0);

    for (auto& note : notes)
    {
        if (!affects(selected, note))
            continue;

        const auto start = frame.toTimeline(note.start);
        const auto end = frame.toTimeline(note.getEnd());
        auto newStart = std::max(frame.origin, moveBy(start, strength, grid.snap(start)));
        auto newEnd = settings.ends ? moveBy(end, strength, grid.snap(end)) : newStart + note.length;
        if (newEnd <= newStart)
            newEnd = grid.lineAfter(newStart);

        note.start = frame.toContent(newStart);
        note.length = newEnd - newStart;
    }
    return notes;
}

std::optional<std::vector<model::Note>> transposed(std::vector<model::Note> notes,
                                                   const NoteIds& selected, int semitones)
{
    for (auto& note : notes)
    {
        if (!affects(selected, note))
            continue;
        note.key += semitones;
        if (note.key < 0 || note.key > 127)
            return std::nullopt;
    }
    return notes;
}

NoteDragResult movedNotes(const std::vector<model::Note>& original, const NoteIds& selected,
                          std::int32_t grabbed, model::Ticks pointerDelta, int keyDelta,
                          const ClipFrame& frame, const Snapper& snap, bool copy)
{
    NoteDragResult result{original, {}};
    const auto* anchor = findNote(original, grabbed);
    if (anchor == nullptr)
    {
        result.selection = selected;
        return result;
    }

    // How far the grabbed note's start moves, and the keys; then the limits of every
    // moving note.
    const auto anchorStart = frame.toTimeline(anchor->start);
    model::Ticks timeDelta = snap(anchorStart + pointerDelta) - anchorStart;
    int lowest = 127;
    int highest = 0;
    model::Ticks earliest = std::numeric_limits<model::Ticks>::max();
    for (const auto& note : original)
    {
        if (!selected.contains(note.id) && note.id != grabbed)
            continue;
        lowest = std::min(lowest, note.key);
        highest = std::max(highest, note.key);
        earliest = std::min(earliest, note.start);
    }
    timeDelta = std::max(timeDelta, -earliest);
    keyDelta = std::clamp(keyDelta, -lowest, 127 - highest);

    std::int32_t nextId = 0;
    for (const auto& note : original)
        nextId = std::max(nextId, note.id);

    for (const auto& note : original)
    {
        if (!selected.contains(note.id) && note.id != grabbed)
            continue;

        auto moved = note;
        moved.start += timeDelta;
        moved.key += keyDelta;
        if (copy)
        {
            moved.id = ++nextId;
            result.notes.push_back(moved);
        }
        else
        {
            for (auto& existing : result.notes)
                if (existing.id == note.id)
                    existing = moved;
        }
        result.selection.insert(moved.id);
    }
    return result;
}

std::vector<model::Note> resizedEnds(const std::vector<model::Note>& original,
                                     const NoteIds& selected, std::int32_t grabbed,
                                     model::Ticks pointerDelta, const ClipFrame& frame,
                                     const Snapper& snap)
{
    const auto* anchor = findNote(original, grabbed);
    if (anchor == nullptr)
        return original;

    const auto anchorEnd = frame.toTimeline(anchor->getEnd());
    const model::Ticks delta = snap(anchorEnd + pointerDelta) - anchorEnd;

    auto notes = original;
    for (auto& note : notes)
    {
        if (!selected.contains(note.id) && note.id != grabbed)
            continue;
        const auto start = frame.toTimeline(note.start);
        const auto shortest = snap.isSnapping() ? snap.lineAfter(start) - start : model::Ticks{1};
        note.length = std::max(note.length + delta, std::max(model::Ticks{1}, shortest));
    }
    return notes;
}

std::vector<model::Note> resizedStarts(const std::vector<model::Note>& original,
                                       const NoteIds& selected, std::int32_t grabbed,
                                       model::Ticks pointerDelta, const ClipFrame& frame,
                                       const Snapper& snap)
{
    const auto* anchor = findNote(original, grabbed);
    if (anchor == nullptr)
        return original;

    const auto anchorStart = frame.toTimeline(anchor->start);
    const model::Ticks delta = snap(anchorStart + pointerDelta) - anchorStart;

    auto notes = original;
    for (auto& note : notes)
    {
        if (!selected.contains(note.id) && note.id != grabbed)
            continue;
        const auto end = note.getEnd();
        note.start = std::clamp(note.start + delta, model::Ticks{0}, end - 1);
        note.length = end - note.start;
    }
    return notes;
}

std::vector<model::Note> withVelocity(std::vector<model::Note> notes, const NoteIds& selected,
                                      int velocity)
{
    for (auto& note : notes)
        if (affects(selected, note))
            note.velocity = std::clamp(velocity, model::Note::minimumVelocity,
                                       model::Note::maximumVelocity);
    return notes;
}

std::vector<model::Note> velocitiesAlong(std::vector<model::Note> notes, const NoteIds& selected,
                                         const ClipFrame& frame, model::Ticks from,
                                         int fromVelocity, model::Ticks to, int toVelocity)
{
    if (to < from)
    {
        std::swap(from, to);
        std::swap(fromVelocity, toVelocity);
    }

    for (auto& note : notes)
    {
        const auto start = frame.toTimeline(note.start);
        if (!affects(selected, note) || start < from || start > to)
            continue;
        const double fraction =
            to > from ? static_cast<double>(start - from) / static_cast<double>(to - from) : 0.0;
        note.velocity = std::clamp(static_cast<int>(std::lround(
                                       fromVelocity + fraction * (toVelocity - fromVelocity))),
                                   model::Note::minimumVelocity, model::Note::maximumVelocity);
    }
    return notes;
}

NoteIds notesIn(const std::vector<model::Note>& notes, const ClipFrame& frame, model::Ticks from,
                model::Ticks to, int lowKey, int highKey)
{
    if (to < from)
        std::swap(from, to);
    if (highKey < lowKey)
        std::swap(lowKey, highKey);

    NoteIds inside;
    for (const auto& note : notes)
    {
        const auto start = frame.toTimeline(note.start);
        const auto end = frame.toTimeline(note.getEnd());
        if (note.key >= lowKey && note.key <= highKey && start < std::max(to, from + 1) && end > from)
            inside.insert(note.id);
    }
    return inside;
}

model::Note newNoteAt(model::Ticks position, int key, model::Ticks length, int velocity,
                      const ClipFrame& frame, const Snapper& snap)
{
    model::Note note;
    note.start = std::max(model::Ticks{0}, frame.toContent(snap.lineAtOrBefore(position)));
    note.length = std::max(model::Ticks{1}, length);
    note.key = std::clamp(key, 0, 127);
    note.velocity = std::clamp(velocity, model::Note::minimumVelocity, model::Note::maximumVelocity);
    return note;
}

NoteHit hitNote(const std::vector<model::Note>& notes, const ClipFrame& frame,
                model::Ticks position, int key, model::Ticks edgeTicks)
{
    NoteHit hit;
    model::Ticks latestStart = std::numeric_limits<model::Ticks>::min();
    for (const auto& note : notes)
    {
        const auto start = frame.toTimeline(note.start);
        const auto end = frame.toTimeline(note.getEnd());
        if (note.key != key || position < start || position >= end || start < latestStart)
            continue;

        latestStart = start;
        hit.id = note.id;
        if (end - position <= edgeTicks)
            hit.part = NotePart::end;
        else if (position - start <= edgeTicks)
            hit.part = NotePart::start;
        else
            hit.part = NotePart::body;
    }
    return hit;
}

std::vector<model::ControllerEvent> eventsAlongLine(std::vector<model::ControllerEvent> events,
                                                    const model::ControllerType& type,
                                                    const ClipFrame& frame, model::Ticks from,
                                                    int fromValue, model::Ticks to, int toValue,
                                                    model::Ticks spacing)
{
    if (to < from)
    {
        std::swap(from, to);
        std::swap(fromValue, toValue);
    }
    const int maximum = type.maximumValue();
    fromValue = std::clamp(fromValue, 0, maximum);
    toValue = std::clamp(toValue, 0, maximum);
    from = std::max(from, frame.origin);
    to = std::max(to, from);
    spacing = std::max(model::Ticks{1}, spacing);

    events.erase(std::remove_if(events.begin(), events.end(),
                                [&](const model::ControllerEvent& event) {
                                    const auto position = frame.toTimeline(event.position);
                                    return event.type == type && position >= from && position <= to;
                                }),
                 events.end());

    int last = -1;
    for (model::Ticks position = from;; position = std::min(position + spacing, to))
    {
        const double fraction =
            to > from ? static_cast<double>(position - from) / static_cast<double>(to - from) : 1.0;
        const int value =
            static_cast<int>(std::lround(fromValue + fraction * (toValue - fromValue)));
        if (value != last || position == to)
            events.push_back({type, frame.toContent(position), value});
        last = value;
        if (position == to)
            break;
    }

    model::normalise(events);
    return events;
}

int hitControllerEvent(const std::vector<model::ControllerEvent>& events, const ClipFrame& frame,
                       model::Ticks position, int value, model::Ticks tickTolerance,
                       int valueTolerance)
{
    int best = -1;
    double bestDistance = std::numeric_limits<double>::max();
    for (std::size_t i = 0; i < events.size(); ++i)
    {
        const auto dt = std::abs(frame.toTimeline(events[i].position) - position);
        const auto dv = std::abs(events[i].value - value);
        if (dt > tickTolerance || dv > valueTolerance)
            continue;
        const double distance =
            static_cast<double>(dt) / static_cast<double>(std::max(model::Ticks{1}, tickTolerance)) +
            static_cast<double>(dv) / static_cast<double>(std::max(1, valueTolerance));
        if (distance < bestDistance)
        {
            bestDistance = distance;
            best = static_cast<int>(i);
        }
    }
    return best;
}

int controllerValueAt(const std::vector<model::ControllerEvent>& events,
                      const model::ControllerType& type, const ClipFrame& frame,
                      model::Ticks position)
{
    int value = type.restValue();
    for (const auto& event : events)
        if (event.type == type && frame.toTimeline(event.position) <= position)
            value = event.value;
    return value;
}

} // namespace daw::controller
