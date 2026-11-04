#pragma once

#include "daw/model/Automation.h"
#include "daw/model/Clip.h"
#include "daw/model/Effect.h"
#include "daw/model/MidiClip.h"
#include "daw/model/Send.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <optional>

namespace daw::model
{

/** The device inputs a track records and monitors. */
struct TrackInput
{
    /** Input channels a track can use: 0 to maximumChannels - 1. */
    static constexpr int maximumChannels = 64;

    int channel = 0;     ///< the first device input channel (0 is input 1); -1 for none
    bool stereo = false; ///< channel and channel + 1, a pair starting on an odd input

    [[nodiscard]] static constexpr TrackInput none() noexcept { return {-1, false}; }
    [[nodiscard]] bool isNone() const noexcept { return channel < 0; }
    [[nodiscard]] int getNumChannels() const noexcept { return isNone() ? 0 : (stereo ? 2 : 1); }

    /** None for a negative channel; otherwise the channel kept in range and moved
        to the start of its pair when stereo (inputs 1+2, 3+4, ...). */
    [[nodiscard]] TrackInput normalised() const noexcept;

    bool operator==(const TrackInput&) const = default;
};

/** When a track plays its input instead of its clips:
    - off: never;
    - automatic: while it is armed and the transport is stopped, during the count-in,
      and while it records (with punch, between punch in and punch out);
    - on: always. */
enum class MonitoringMode : std::uint8_t
{
    off,
    automatic,
    on
};

/** Names as stored in project files: "off", "auto", "on". */
[[nodiscard]] juce::String toString(MonitoringMode mode);
[[nodiscard]] std::optional<MonitoringMode> monitoringModeFromString(const juce::String& text);

/** An audio track records and plays audio clips; a MIDI track records and plays MIDI
    clips through its instrument. */
enum class TrackKind : std::uint8_t
{
    audio,
    midi
};

/** Names as stored in project files: "audio", "midi". */
[[nodiscard]] juce::String toString(TrackKind kind);
[[nodiscard]] std::optional<TrackKind> trackKindFromString(const juce::String& text);

/** The live MIDI a MIDI track hears and records: from one device (by name) or every
    one, on one channel or every one. */
struct MidiInput
{
    juce::String device; ///< empty for every device
    int channel = 0;     ///< 1 to 16; 0 for every channel

    bool operator==(const MidiInput&) const = default;
};

/**
    A track: its kind, its mixer settings, its input, its clips, its effects (before
    its fader), its sends to aux buses and its automation. A MIDI track also has an
    instrument (before its effects), a MIDI input and the channel its notes are on in
    MIDI files.

    Clips are kept in stacking order: the last one is on top. Where clips overlap,
    the one on top is heard. A handle onto part of a Project's state, like Clip.
*/
class Track
{
public:
    /** Volume is a linear gain: 0 is silent, 1 is 0 dB, maximumVolume is +6 dB. */
    static constexpr double maximumVolume = 1.9952623149688795; // 10^(6/20)

    explicit Track(juce::ValueTree trackState);

    [[nodiscard]] ObjectId getId() const;

    /** Fixed when the track is made. */
    [[nodiscard]] TrackKind getKind() const;
    [[nodiscard]] bool isMidi() const { return getKind() == TrackKind::midi; }

    [[nodiscard]] juce::String getName() const;
    void setName(const juce::String& newName, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] double getVolume() const;
    void setVolume(double gain, juce::UndoManager* undoManager = nullptr);

    /** -1 is hard left, 0 the centre, 1 hard right. */
    [[nodiscard]] double getPan() const;
    void setPan(double newPan, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] bool isMuted() const;
    void setMuted(bool shouldBeMuted, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] bool isSoloed() const;
    void setSoloed(bool shouldBeSoloed, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] TrackInput getInput() const;
    /** Stored normalised (see TrackInput::normalised()). */
    void setInput(TrackInput input, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] MidiInput getMidiInput() const;
    /** The channel is kept within 0 to 16. */
    void setMidiInput(const MidiInput& input, juce::UndoManager* undoManager = nullptr);

    /** 1 to 16: the channel of the track's notes in MIDI files. */
    [[nodiscard]] int getMidiChannel() const;
    void setMidiChannel(int channel, juce::UndoManager* undoManager = nullptr);

    /** A MIDI track's instrument, if it has one. */
    [[nodiscard]] std::optional<Effect> getInstrument() const;

    /** Replaces the instrument (and the automation of the old one's parameters) with
        a new one of this type, with an id no other object of the project has. */
    Effect setInstrument(const juce::String& type,
                         const std::vector<std::pair<juce::String, double>>& parameters = {},
                         juce::UndoManager* undoManager = nullptr);

    /** Also removes the automation of its parameters. */
    void removeInstrument(juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] MonitoringMode getMonitoring() const;
    void setMonitoring(MonitoringMode mode, juce::UndoManager* undoManager = nullptr);

    /** Armed tracks record when recording starts. Not undoable: a switch flipped
        while working, like loop on/off. */
    [[nodiscard]] bool isArmed() const;
    void setArmed(bool shouldBeArmed);

    [[nodiscard]] int getNumClips() const;
    [[nodiscard]] Clip getClip(int index) const;
    [[nodiscard]] int indexOfClip(ObjectId clipId) const; ///< -1 if not on this track

    /** Adds a clip on top of the others, with an id no other track or clip of the
        project has. */
    Clip addClip(const juce::File& audioFile, const ClipBounds& bounds = {},
                 juce::UndoManager* undoManager = nullptr);

    /** Adds a MIDI clip on top of the others (bounds must have a length), with these
        notes and controller events, with an id no other object of the project has. */
    Clip addMidiClip(const juce::String& name, const ClipBounds& bounds,
                     std::vector<Note> notes = {}, std::vector<ControllerEvent> controllers = {},
                     juce::UndoManager* undoManager = nullptr);

    /** Adds a copy of a clip (of this project) on top of the others, with a new id,
        at newPosition; the copy of a MIDI clip has the same notes and controllers. */
    Clip addCopyOf(const Clip& clip, Ticks newPosition, juce::UndoManager* undoManager = nullptr);
    void removeClip(int index, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] EffectList getEffects() const;
    [[nodiscard]] Automation getAutomation() const;

    [[nodiscard]] int getNumSends() const;
    [[nodiscard]] Send getSend(int index) const;
    [[nodiscard]] int indexOfSend(ObjectId sendId) const; ///< -1 if not on this track
    [[nodiscard]] int indexOfSendTo(ObjectId busId) const; ///< -1 if it does not send there

    /** A post-fader send at 0 dB to the bus, with a new id; or the existing send to
        it (one per bus). */
    Send addSend(ObjectId busId, juce::UndoManager* undoManager = nullptr);

    /** Also removes the automation of its level. */
    void removeSend(int index, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
