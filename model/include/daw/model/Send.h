#pragma once

#include "daw/model/Clip.h"

#include <juce_data_structures/juce_data_structures.h>

namespace daw::model
{

/** A track's send to an aux bus: which bus, at what level (a linear gain, as a
    track's volume: 0 to +6 dB), taken before or after the track's fader. A handle
    onto part of a Project's state. */
class Send
{
public:
    explicit Send(juce::ValueTree sendState);

    [[nodiscard]] ObjectId getId() const;
    [[nodiscard]] ObjectId getBus() const;

    [[nodiscard]] double getLevel() const;
    void setLevel(double gain, juce::UndoManager* undoManager = nullptr);

    /** Pre-fader: after the track's effects, before its volume and pan. */
    [[nodiscard]] bool isPreFader() const;
    void setPreFader(bool shouldBePreFader, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
