#pragma once

#include "daw/model/Clip.h"

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

/**
    A track: its mixer settings, its input and its clips.

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
    void removeClip(int index, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
