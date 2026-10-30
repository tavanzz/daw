#include "daw/model/Track.h"

#include "Identifiers.h"

namespace daw::model
{

Track::Track(juce::ValueTree trackState)
    : state(std::move(trackState))
{
    jassert(state.hasType(ids::track));
}

juce::String Track::getName() const
{
    return state[ids::name].toString();
}

void Track::setName(const juce::String& newName, juce::UndoManager* undoManager)
{
    state.setProperty(ids::name, newName, undoManager);
}

int Track::getNumClips() const
{
    return state.getNumChildren();
}

Clip Track::getClip(int index) const
{
    jassert(index >= 0 && index < getNumClips());
    return Clip(state.getChild(index));
}

Clip Track::addClip(const juce::File& audioFile, juce::UndoManager* undoManager)
{
    Clip clip{juce::ValueTree(ids::clip)};
    clip.setAudioFile(audioFile);
    state.appendChild(clip.getState(), undoManager);
    return clip;
}

void Track::removeClip(int index, juce::UndoManager* undoManager)
{
    state.removeChild(index, undoManager);
}

} // namespace daw::model
