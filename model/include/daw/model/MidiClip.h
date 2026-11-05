#pragma once

#include "daw/model/Clip.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <optional>
#include <vector>

namespace daw::model
{

/** A note of a MIDI clip. Its start counts from the start of the clip's content (see
    Clip: the clip shows its content from its offset on). Key and velocity are MIDI
    1.0 values. */
struct Note
{
    static constexpr int minimumVelocity = 1;
    static constexpr int maximumVelocity = 127;
    static constexpr int defaultVelocity = 100;

    /** Unique within its clip, never 0 once the note is in a clip. */
    std::int32_t id = 0;
    Ticks start = 0;
    Ticks length = 1; ///< at least 1
    int key = 60;     ///< 0 to 127; 60 is middle C (C3)
    int velocity = defaultVelocity;

    [[nodiscard]] Ticks getEnd() const noexcept { return start + length; }

    bool operator==(const Note&) const = default;
};

/** Which controller a lane moves: a control change by number, the pitch bend or the
    channel pressure (aftertouch). */
struct ControllerType
{
    enum class Kind : std::uint8_t
    {
        controlChange,
        pitchBend,
        channelPressure
    };

    Kind kind = Kind::controlChange;
    int number = 0; ///< 0 to 127, for control changes only

    [[nodiscard]] static ControllerType controlChange(int number)
    {
        return {Kind::controlChange, number};
    }
    [[nodiscard]] static ControllerType pitchBend() { return {Kind::pitchBend, 0}; }
    [[nodiscard]] static ControllerType channelPressure() { return {Kind::channelPressure, 0}; }

    /** The largest value: 127, or 16383 for the pitch bend. */
    [[nodiscard]] int maximumValue() const noexcept { return kind == Kind::pitchBend ? 16383 : 127; }

    /** Where it rests when nothing moves it: the pitch bend centred (8192), volume
        (CC7) and expression (CC11) full, everything else 0. */
    [[nodiscard]] int restValue() const noexcept;

    /** As stored in project files: "cc:1", "pitchBend", "aftertouch". */
    [[nodiscard]] juce::String toString() const;
    [[nodiscard]] static std::optional<ControllerType> fromString(const juce::String& text);

    bool operator==(const ControllerType&) const = default;
    bool operator<(const ControllerType& other) const noexcept
    {
        return kind != other.kind ? kind < other.kind : number < other.number;
    }
};

/** A controller's value from a position of the clip's content on, as MIDI 1.0 sends
    it (0 to 127; the pitch bend 0 to 16383). */
struct ControllerEvent
{
    ControllerType type;
    Ticks position = 0;
    int value = 0;

    bool operator==(const ControllerEvent&) const = default;
};

/**
    The notes and controller events of a MIDI clip (a Clip whose isMidi() is true).

    Each list is kept whole in the clip's state and replaced as a whole by every edit:
    one undo step per edit, however many notes it touches, and cheap to read for
    thousands of notes. Notes are kept sorted by start, then key; controller events by
    type, then position (events of one type at one position keep their order).

    A handle onto part of a Project's state, like Clip.
*/
class MidiClip
{
public:
    explicit MidiClip(juce::ValueTree clipState);

    [[nodiscard]] Clip getClip() const { return Clip(state); }

    [[nodiscard]] std::vector<Note> getNotes() const;

    /** Replaces the notes: values clamped (start 0 or later, length 1 or more, key
        0 to 127, velocity 1 to 127), sorted; notes with id 0 (or an id another
        note has) get new ids. */
    void setNotes(std::vector<Note> notes, juce::UndoManager* undoManager = nullptr);

    /** The note with that id, if the clip has it. */
    [[nodiscard]] std::optional<Note> findNote(std::int32_t noteId) const;

    [[nodiscard]] std::vector<ControllerEvent> getControllerEvents() const;

    /** The events of one type, by position. */
    [[nodiscard]] std::vector<ControllerEvent> getControllerEvents(const ControllerType& type) const;

    /** Every type with events, in order. */
    [[nodiscard]] std::vector<ControllerType> getControllerTypes() const;

    /** Replaces the controller events: values clamped, sorted. */
    void setControllerEvents(std::vector<ControllerEvent> events,
                             juce::UndoManager* undoManager = nullptr);

    /** Replaces the events of one type, keeping the others. */
    void setControllerEvents(const ControllerType& type, std::vector<ControllerEvent> events,
                             juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

/** Notes and controller events in a canonical order (see MidiClip); values clamped
    and ids made unique (0 or repeated ids get the largest + 1). */
void normalise(std::vector<Note>& notes);
void normalise(std::vector<ControllerEvent>& events);

} // namespace daw::model
