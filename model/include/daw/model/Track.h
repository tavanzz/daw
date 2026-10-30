#pragma once

#include "daw/model/Clip.h"

#include <juce_data_structures/juce_data_structures.h>

#include <optional>

namespace daw::model
{

/**
    A track: its mixer settings and its clips.

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
