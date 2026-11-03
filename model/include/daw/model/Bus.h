#pragma once

#include "daw/model/Automation.h"
#include "daw/model/Effect.h"

#include <juce_data_structures/juce_data_structures.h>

namespace daw::model
{

/** An aux bus: what tracks send to it goes through its effects and its fader (volume
    and pan, as a track's) into the master. A handle onto part of a Project's state. */
class Bus
{
public:
    explicit Bus(juce::ValueTree busState);

    [[nodiscard]] ObjectId getId() const;
    [[nodiscard]] juce::String getName() const;
    void setName(const juce::String& newName, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] double getVolume() const; ///< linear gain, as Track::getVolume()
    void setVolume(double gain, juce::UndoManager* undoManager = nullptr);
    [[nodiscard]] double getPan() const;
    void setPan(double newPan, juce::UndoManager* undoManager = nullptr);
    [[nodiscard]] bool isMuted() const;
    void setMuted(bool shouldBeMuted, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] EffectList getEffects() const;
    [[nodiscard]] Automation getAutomation() const;

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
