#pragma once

#include <juce_data_structures/juce_data_structures.h>

namespace daw::model
{

/** One clip on a track. In Phase 1 a clip is a whole audio file.

    A lightweight handle onto part of a Project's state: copies refer to the same
    clip, and changes go straight into the project. */
class Clip
{
public:
    explicit Clip(juce::ValueTree clipState);

    /** The audio file, as an absolute path (project files store it relative to the
        project when they can). */
    [[nodiscard]] juce::File getAudioFile() const;
    void setAudioFile(const juce::File& file, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
