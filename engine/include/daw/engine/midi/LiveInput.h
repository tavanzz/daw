#pragma once

#include "daw/engine/midi/EventBuffer.h"
#include "daw/engine/midi/HeldNotes.h"

#include <juce_core/juce_core.h>

#include <array>
#include <cstdint>
#include <span>

namespace daw::engine::midi
{

/** The most MIDI input devices told apart (devices beyond share the last index). */
inline constexpr int maximumInputDevices = 32;

/** A MIDI message as a device delivered it: when (seconds on the host clock, see
    hostSeconds()), from which device (an index the device host gives each), and its
    bytes. */
struct TimedMessage
{
    double time = 0.0;
    std::uint8_t device = 0;
    std::uint8_t size = 0;
    std::array<std::uint8_t, 3> bytes{};
};

/** The clock MIDI timestamps and audio blocks are compared on: seconds, as JUCE
    stamps incoming MIDI (Time::getMillisecondCounterHiRes() / 1000). Any thread. */
[[nodiscard]] double hostSeconds() noexcept;

/** A channel message as an event (a note-on with velocity 0 is a note-off); false
    for anything else (system messages, program changes, polyphonic pressure).
    Any thread. */
bool parse(const TimedMessage& message, Event& event) noexcept;

/** An event from outside the timeline, placed in the current audio block: from a
    device (track 0) or the piano roll's preview of one track. Its offset counts
    from the start of the device's block. */
struct LiveEvent
{
    Event event;
    std::int16_t device = -1; ///< the device's index; -1 for a preview
    juce::int64 track = 0;    ///< for a preview, the track it plays on
};

using LiveEventSpan = std::span<const LiveEvent>;

/** Which live input a MIDI track hears. */
struct InputFilter
{
    static constexpr int everyDevice = -1;
    static constexpr int noDevice = -2; ///< a device that is not connected

    int device = everyDevice; ///< a device index, everyDevice or noDevice
    int channel = -1;         ///< 0 to 15; -1 for every channel

    [[nodiscard]] bool accepts(const LiveEvent& live) const noexcept
    {
        return live.device >= 0 && (device == everyDevice || device == live.device) &&
               (channel < 0 || channel == live.event.channel);
    }

    bool operator==(const InputFilter&) const = default;
};

/**
    The notes a track plays from live input (devices and previews), so that each gets
    its note-off whatever happens: a track that stops listening (disarmed, monitoring
    switched off, another device chosen, removed) ends them at once and returns the
    controllers it moved to rest.

    Devices send MIDI 1.0 without note ids: each note gets one of its own here (apart
    from the timeline's), and a note-off ends the oldest held note on its key.

    Fixed size, real-time safe; one thread only.
*/
class LiveNotes
{
public:
    /** Adds event to events if it may be sent (a note-off only for a held note). */
    void receive(Event event, EventBuffer& events) noexcept;

    /** Ends every held note at offset and rests the controllers moved. */
    void releaseAll(int offset, EventBuffer& events) noexcept;

    void adopt(LiveNotes& previous) noexcept;

    [[nodiscard]] int getNumHeldNotes() const noexcept { return held.size(); }

private:
    static constexpr int maximumControllers = 32;

    struct Moved
    {
        EventType type = EventType::controller;
        std::uint8_t number = 0;
        std::uint8_t channel = 0;
    };

    HeldNotes held;
    std::array<Moved, maximumControllers> moved{};
    int movedCount = 0;
    std::int32_t nextNoteId = firstNoteId;

    // Live notes' ids stay apart from the timeline's (which count from 0).
    static constexpr std::int32_t firstNoteId = 1 << 30;
};

} // namespace daw::engine::midi
