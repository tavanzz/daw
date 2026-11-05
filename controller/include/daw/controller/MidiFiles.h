#pragma once

#include "daw/model/Project.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace daw::controller
{

/** Pulses per quarter note of the MIDI files the app writes. */
inline constexpr int midiFileResolution = 960;

/** What a standard MIDI file holds, as the app reads it: per track and channel, the
    notes and controllers in ticks of the project's timeline (beats at the project's
    tempo), and the file's first tempo and metre. */
struct MidiFileContent
{
    struct Part
    {
        juce::String name;
        int channel = 1; ///< 1 to 16
        std::vector<model::Note> notes;
        std::vector<model::ControllerEvent> controllers;
        model::Ticks end = 0; ///< where its last note or event ends
    };

    std::vector<Part> parts;
    int type = 1;
    double tempo = 120.0;
    model::TimeSignature timeSignature{4, 4};
    bool hasTempo = false;
    bool hasTimeSignature = false;

    /** What the app leaves out, for the user (tempo changes, program changes...). */
    juce::StringArray leftOut;
};

/**
    Reads a standard MIDI file (type 0 or 1; PPQ or SMPTE time). Positions become
    beats at tempoMap's tempo (the music stays on the project's bars); each track is
    split by channel; a note-on ends at the next note-off of its key and channel
    (one never ended at its track's end). Pitch bend, channel pressure and every
    control change but the channel mode messages (120 to 127) are kept. Returns an
    error for a file that is not a MIDI file.
*/
[[nodiscard]] juce::Result readMidiFile(const juce::File& file, const model::TempoMap& tempoMap,
                                        MidiFileContent& content);

/** The same from bytes. */
[[nodiscard]] juce::Result readMidiFile(const juce::MemoryBlock& bytes,
                                        const model::TempoMap& tempoMap, MidiFileContent& content);

/**
    A standard MIDI file of a project's MIDI tracks: what each plays (clips where
    they are heard, notes cut where their clip's visible part ends, controllers
    returning to rest after each clip), on its channel, named after it, with the
    project's tempo and metre, at midiFileResolution pulses per quarter note. Type 1:
    a conductor track, then one track per MIDI track; type 0: everything in one.
    Muting and solo are ignored: every MIDI track is written.
*/
[[nodiscard]] juce::MemoryBlock writeMidiFile(const model::Project& project, int type);

} // namespace daw::controller
