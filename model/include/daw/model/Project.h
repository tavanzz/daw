#pragma once

#include "daw/model/Track.h"

#include <juce_data_structures/juce_data_structures.h>

namespace daw::model
{

/**
    Everything a project file stores: its tracks, their clips, and the loop setting.

    The state is a juce::ValueTree, so later phases get undo/redo (pass an
    UndoManager) and change notifications (ValueTree::Listener) without changing
    this API. Message thread only; the engine never sees the model directly.
*/
class Project
{
public:
    /** An empty project: no tracks, loop off. */
    Project();

    /** Wraps existing project state, as built by ProjectFile. */
    explicit Project(juce::ValueTree projectState);

    [[nodiscard]] int getNumTracks() const;
    [[nodiscard]] Track getTrack(int index) const;
    Track addTrack(const juce::String& name, juce::UndoManager* undoManager = nullptr);
    void removeTrack(int index, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] bool isLoopEnabled() const;
    void setLoopEnabled(bool shouldLoop, juce::UndoManager* undoManager = nullptr);

    /** Same tracks, clips and settings, in the same order. */
    [[nodiscard]] bool isEquivalentTo(const Project& other) const;

    [[nodiscard]] juce::ValueTree& getState() noexcept { return state; }
    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
