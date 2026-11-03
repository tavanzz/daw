#include "daw/model/Bus.h"

#include "daw/model/Track.h"

#include "Identifiers.h"

#include <algorithm>
#include <cmath>

namespace daw::model
{

Bus::Bus(juce::ValueTree busState)
    : state(std::move(busState))
{
    jassert(state.hasType(ids::bus));
}

ObjectId Bus::getId() const
{
    return static_cast<ObjectId>(state[ids::id]);
}

juce::String Bus::getName() const
{
    return state[ids::name].toString();
}

void Bus::setName(const juce::String& newName, juce::UndoManager* undoManager)
{
    state.setProperty(ids::name, newName, undoManager);
}

double Bus::getVolume() const
{
    return static_cast<double>(state.getProperty(ids::volume, 1.0));
}

void Bus::setVolume(double gain, juce::UndoManager* undoManager)
{
    state.setProperty(ids::volume,
                      std::isfinite(gain) ? std::clamp(gain, 0.0, Track::maximumVolume) : 1.0,
                      undoManager);
}

double Bus::getPan() const
{
    return static_cast<double>(state.getProperty(ids::pan, 0.0));
}

void Bus::setPan(double newPan, juce::UndoManager* undoManager)
{
    state.setProperty(ids::pan, std::isfinite(newPan) ? std::clamp(newPan, -1.0, 1.0) : 0.0,
                      undoManager);
}

bool Bus::isMuted() const
{
    return static_cast<bool>(state[ids::mute]);
}

void Bus::setMuted(bool shouldBeMuted, juce::UndoManager* undoManager)
{
    state.setProperty(ids::mute, shouldBeMuted, undoManager);
}

EffectList Bus::getEffects() const
{
    return EffectList(state.getChildWithName(ids::effects));
}

Automation Bus::getAutomation() const
{
    return Automation(state.getChildWithName(ids::automation));
}

} // namespace daw::model
