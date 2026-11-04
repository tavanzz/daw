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

juce::String toString(TrackKind kind)
{
    return kind == TrackKind::midi ? "midi" : "audio";
}

std::optional<TrackKind> trackKindFromString(const juce::String& text)
{
    if (text == "audio")
        return TrackKind::audio;
    if (text == "midi")
        return TrackKind::midi;
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

TrackKind Track::getKind() const
{
    return trackKindFromString(state[ids::kind].toString()).value_or(TrackKind::audio);
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

MidiInput Track::getMidiInput() const
{
    return {state[ids::midiInputDevice].toString(),
            std::clamp(static_cast<int>(state[ids::midiInputChannel]), 0, 16)};
}

void Track::setMidiInput(const MidiInput& input, juce::UndoManager* undoManager)
{
    state.setProperty(ids::midiInputDevice, input.device, undoManager);
    state.setProperty(ids::midiInputChannel, std::clamp(input.channel, 0, 16), undoManager);
}

int Track::getMidiChannel() const
{
    return std::clamp(static_cast<int>(state.getProperty(ids::midiChannel, 1)), 1, 16);
}

void Track::setMidiChannel(int channel, juce::UndoManager* undoManager)
{
    state.setProperty(ids::midiChannel, std::clamp(channel, 1, 16), undoManager);
}

std::optional<Effect> Track::getInstrument() const
{
    const auto container = state.getChildWithName(ids::instrument);
    if (!container.isValid() || container.getNumChildren() == 0)
        return std::nullopt;
    return Effect(container.getChild(0));
}

Effect Track::setInstrument(const juce::String& type,
                            const std::vector<std::pair<juce::String, double>>& parameters,
                            juce::UndoManager* undoManager)
{
    // A new id, even though the old instrument goes: the engine then knows it as a
    // new instrument whatever its type.
    const auto newId = detail::nextObjectId(state);
    removeInstrument(undoManager);

    auto container = state.getChildWithName(ids::instrument);
    if (!container.isValid())
    {
        container = juce::ValueTree(ids::instrument);
        state.appendChild(container, undoManager);
    }

    juce::ValueTree effectState(ids::effect);
    effectState.setProperty(ids::id, newId, nullptr);
    effectState.setProperty(ids::type, type, nullptr);
    effectState.setProperty(ids::bypass, false, nullptr);
    Effect effect(effectState);
    for (const auto& [id, value] : parameters)
        effect.setParameter(id, value);
    container.appendChild(effectState, undoManager);
    return effect;
}

void Track::removeInstrument(juce::UndoManager* undoManager)
{
    auto container = state.getChildWithName(ids::instrument);
    if (!container.isValid() || container.getNumChildren() == 0)
        return;

    getAutomation().removeLanesOfEffect(Effect(container.getChild(0)).getId(), undoManager);
    container.removeAllChildren(undoManager);
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

Clip Track::addMidiClip(const juce::String& name, const ClipBounds& bounds, std::vector<Note> notes,
                        std::vector<ControllerEvent> controllers, juce::UndoManager* undoManager)
{
    jassert(bounds.length.has_value());
    juce::ValueTree clipState(ids::clip);
    clipState.setProperty(ids::id, detail::nextObjectId(state), nullptr);
    clipState.setProperty(ids::kind, "midi", nullptr);

    Clip clip{clipState};
    clip.setName(name);
    clip.setBounds({bounds.position, bounds.offset, bounds.length.value_or(Clip::minimumLength)});
    MidiClip midi{clipState};
    midi.setNotes(std::move(notes));
    midi.setControllerEvents(std::move(controllers));
    state.addChild(clipState, getNumClips(), undoManager);
    return clip;
}

Clip Track::addCopyOf(const Clip& clip, Ticks newPosition, juce::UndoManager* undoManager)
{
    auto copy = clip.getState().createCopy();
    copy.setProperty(ids::id, detail::nextObjectId(state), nullptr);
    Clip copied{copy};
    copied.setPosition(newPosition);
    state.addChild(copy, getNumClips(), undoManager);
    return copied;
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
