#include "daw/model/Track.h"

#include "Containers.h"
#include "Identifiers.h"
#include "ObjectIds.h"

#include <algorithm>
#include <cmath>

namespace daw::model
{

TrackInput TrackInput::normalised() const noexcept
{
    if (channel < 0)
        return none();

    const int first = std::min(channel, maximumChannels - 1);
    return {stereo ? first - first % 2 : first, stereo};
}

juce::String toString(MonitoringMode mode)
{
    switch (mode)
    {
    case MonitoringMode::off:
        return "off";
    case MonitoringMode::automatic:
        return "auto";
    case MonitoringMode::on:
        return "on";
    }

    return "auto";
}

std::optional<MonitoringMode> monitoringModeFromString(const juce::String& text)
{
    for (const auto mode : {MonitoringMode::off, MonitoringMode::automatic, MonitoringMode::on})
        if (toString(mode) == text)
            return mode;

    return std::nullopt;
}

Track::Track(juce::ValueTree trackState)
    : state(std::move(trackState))
{
    jassert(state.hasType(ids::track));
}

ObjectId Track::getId() const
{
    return static_cast<ObjectId>(state[ids::id]);
}

juce::String Track::getName() const
{
    return state[ids::name].toString();
}

void Track::setName(const juce::String& newName, juce::UndoManager* undoManager)
{
    state.setProperty(ids::name, newName, undoManager);
}

double Track::getVolume() const
{
    return static_cast<double>(state.getProperty(ids::volume, 1.0));
}

void Track::setVolume(double gain, juce::UndoManager* undoManager)
{
    state.setProperty(ids::volume, std::isfinite(gain) ? std::clamp(gain, 0.0, maximumVolume) : 1.0,
                      undoManager);
}

double Track::getPan() const
{
    return static_cast<double>(state.getProperty(ids::pan, 0.0));
}

void Track::setPan(double newPan, juce::UndoManager* undoManager)
{
    state.setProperty(ids::pan, std::isfinite(newPan) ? std::clamp(newPan, -1.0, 1.0) : 0.0,
                      undoManager);
}

bool Track::isMuted() const
{
    return static_cast<bool>(state[ids::mute]);
}

void Track::setMuted(bool shouldBeMuted, juce::UndoManager* undoManager)
{
    state.setProperty(ids::mute, shouldBeMuted, undoManager);
}

bool Track::isSoloed() const
{
    return static_cast<bool>(state[ids::solo]);
}

void Track::setSoloed(bool shouldBeSoloed, juce::UndoManager* undoManager)
{
    state.setProperty(ids::solo, shouldBeSoloed, undoManager);
}

TrackInput Track::getInput() const
{
    return TrackInput{static_cast<int>(state.getProperty(ids::inputChannel, 0)),
                      static_cast<bool>(state.getProperty(ids::inputStereo, false))}
        .normalised();
}

void Track::setInput(TrackInput input, juce::UndoManager* undoManager)
{
    const auto stored = input.normalised();
    state.setProperty(ids::inputChannel, stored.channel, undoManager);
    state.setProperty(ids::inputStereo, stored.stereo, undoManager);
}

MonitoringMode Track::getMonitoring() const
{
    return monitoringModeFromString(state[ids::monitoring].toString())
        .value_or(MonitoringMode::automatic);
}

void Track::setMonitoring(MonitoringMode mode, juce::UndoManager* undoManager)
{
    state.setProperty(ids::monitoring, toString(mode), undoManager);
}

bool Track::isArmed() const
{
    return static_cast<bool>(state[ids::armed]);
}

void Track::setArmed(bool shouldBeArmed)
{
    state.setProperty(ids::armed, shouldBeArmed, nullptr);
}

int Track::getNumClips() const
{
    return detail::countLeading(state, ids::clip);
}

Clip Track::getClip(int index) const
{
    jassert(index >= 0 && index < getNumClips());
    return Clip(state.getChild(index));
}

int Track::indexOfClip(ObjectId clipId) const
{
    for (int index = 0; index < getNumClips(); ++index)
        if (static_cast<ObjectId>(state.getChild(index)[ids::id]) == clipId)
            return index;

    return -1;
}

Clip Track::addClip(const juce::File& audioFile, const ClipBounds& bounds,
                    juce::UndoManager* undoManager)
{
    juce::ValueTree clipState(ids::clip);
    clipState.setProperty(ids::id, detail::nextObjectId(state), nullptr);

    Clip clip{clipState};
    clip.setAudioFile(audioFile);
    clip.setBounds(bounds);
    state.addChild(clip.getState(), getNumClips(), undoManager);
    return clip;
}

void Track::removeClip(int index, juce::UndoManager* undoManager)
{
    if (index >= 0 && index < getNumClips())
        state.removeChild(index, undoManager);
}

EffectList Track::getEffects() const
{
    return EffectList(state.getChildWithName(ids::effects));
}

Automation Track::getAutomation() const
{
    return Automation(state.getChildWithName(ids::automation));
}

int Track::getNumSends() const
{
    return state.getChildWithName(ids::sends).getNumChildren();
}

Send Track::getSend(int index) const
{
    jassert(index >= 0 && index < getNumSends());
    return Send(state.getChildWithName(ids::sends).getChild(index));
}

int Track::indexOfSend(ObjectId sendId) const
{
    for (int index = 0; index < getNumSends(); ++index)
        if (getSend(index).getId() == sendId)
            return index;
    return -1;
}

int Track::indexOfSendTo(ObjectId busId) const
{
    for (int index = 0; index < getNumSends(); ++index)
        if (getSend(index).getBus() == busId)
            return index;
    return -1;
}

Send Track::addSend(ObjectId busId, juce::UndoManager* undoManager)
{
    if (const int existing = indexOfSendTo(busId); existing >= 0)
        return getSend(existing);

    juce::ValueTree sendState(ids::send);
    sendState.setProperty(ids::id, detail::nextObjectId(state), nullptr);
    sendState.setProperty(ids::busId, busId, nullptr);
    sendState.setProperty(ids::level, 1.0, nullptr);
    sendState.setProperty(ids::preFader, false, nullptr);
    state.getChildWithName(ids::sends).appendChild(sendState, undoManager);
    return Send(sendState);
}

void Track::removeSend(int index, juce::UndoManager* undoManager)
{
    if (index < 0 || index >= getNumSends())
        return;

    getAutomation().removeLaneOfSend(getSend(index).getId(), undoManager);
    state.getChildWithName(ids::sends).removeChild(index, undoManager);
}

} // namespace daw::model
