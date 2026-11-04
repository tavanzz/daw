#pragma once

#include "daw/controller/RecordingController.h"
#include "daw/model/MidiClip.h"

#include <vector>

namespace daw::controller
{

/** A MIDI recording as a clip's content: positions from the take's first position
    (the count-in's start), in ticks. */
struct MidiTakeContent
{
    std::vector<model::Note> notes;
    std::vector<model::ControllerEvent> controllers;

    [[nodiscard]] bool isEmpty() const noexcept { return notes.empty() && controllers.empty(); }
};

/**
    Turns what a MIDI track recorded into notes and controller events: each note-on
    ends at the next note-off of its key and channel (a note still held ends at
    stopPosition); a note shorter than a tick gets one; controllers keep MIDI 1.0
    values. Channels are not kept: the track has one. Positions in samples at
    sampleRate become exact ticks.
*/
[[nodiscard]] MidiTakeContent midiTakeContent(const RecordedMidiTake& take, juce::int64 stopPosition,
                                              double sampleRate);

} // namespace daw::controller
