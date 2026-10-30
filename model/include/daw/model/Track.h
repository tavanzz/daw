#pragma once

#include "daw/model/Clip.h"

#include <juce_data_structures/juce_data_structures.h>

namespace daw::model
{

/** A track and its clips. A handle onto part of a Project's state, like Clip. */
class Track
{
public:
    explicit Track(juce::ValueTree trackState);

    [[nodiscard]] juce::String getName() const;
    void setName(const juce::String& newName, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] int getNumClips() const;
    [[nodiscard]] Clip getClip(int index) const;
    Clip addClip(const juce::File& audioFile, juce::UndoManager* undoManager = nullptr);
    void removeClip(int index, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
