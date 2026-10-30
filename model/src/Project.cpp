#include "daw/model/Project.h"

#include "Identifiers.h"

namespace daw::model
{

Project::Project()
    : state(ids::project)
{
    state.setProperty(ids::loopEnabled, false, nullptr);
}

Project::Project(juce::ValueTree projectState)
    : state(std::move(projectState))
{
    jassert(state.hasType(ids::project));
}

int Project::getNumTracks() const
{
    return state.getNumChildren();
}

Track Project::getTrack(int index) const
{
    jassert(index >= 0 && index < getNumTracks());
    return Track(state.getChild(index));
}

Track Project::addTrack(const juce::String& name, juce::UndoManager* undoManager)
{
    Track track{juce::ValueTree(ids::track)};
    track.setName(name);
    state.appendChild(track.getState(), undoManager);
    return track;
}

void Project::removeTrack(int index, juce::UndoManager* undoManager)
{
    state.removeChild(index, undoManager);
}

bool Project::isLoopEnabled() const
{
    return static_cast<bool>(state[ids::loopEnabled]);
}

void Project::setLoopEnabled(bool shouldLoop, juce::UndoManager* undoManager)
{
    state.setProperty(ids::loopEnabled, shouldLoop, undoManager);
}

bool Project::isEquivalentTo(const Project& other) const
{
    return state.isEquivalentTo(other.state);
}

} // namespace daw::model
