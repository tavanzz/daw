#include "daw/model/Project.h"

#include "Identifiers.h"
#include "ObjectIds.h"

#include <algorithm>
#include <cmath>

namespace daw::model
{

Project::Project()
    : state(ids::project)
{
    state.setProperty(ids::tempo, TempoMap::defaultTempo, nullptr);
    state.setProperty(ids::timeSignatureNumerator, 4, nullptr);
    state.setProperty(ids::timeSignatureDenominator, 4, nullptr);
    state.setProperty(ids::loopEnabled, false, nullptr);
    state.setProperty(ids::loopStart, Ticks{0}, nullptr);
    state.setProperty(ids::loopEnd, Ticks{0}, nullptr);
    state.setProperty(ids::snapEnabled, true, nullptr);
    state.setProperty(ids::gridDivision, toString(GridDivision::automatic), nullptr);
    state.setProperty(ids::masterVolume, 1.0, nullptr);
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

int Project::indexOfTrack(ObjectId trackId) const
{
    for (int index = 0; index < getNumTracks(); ++index)
        if (getTrack(index).getId() == trackId)
            return index;

    return -1;
}

Track Project::addTrack(const juce::String& name, juce::UndoManager* undoManager)
{
    juce::ValueTree trackState(ids::track);
    trackState.setProperty(ids::id, detail::nextObjectId(state), nullptr);

    Track track{trackState};
    track.setName(name);
    track.setVolume(1.0);
    track.setPan(0.0);
    track.setMuted(false);
    track.setSoloed(false);
    state.appendChild(trackState, undoManager);
    return track;
}

void Project::removeTrack(int index, juce::UndoManager* undoManager)
{
    state.removeChild(index, undoManager);
}

bool Project::isAnyTrackSoloed() const
{
    for (int index = 0; index < getNumTracks(); ++index)
        if (getTrack(index).isSoloed())
            return true;

    return false;
}

bool Project::isTrackAudible(int index) const
{
    const auto track = getTrack(index);
    return !track.isMuted() && (track.isSoloed() || !isAnyTrackSoloed());
}

std::optional<ClipLocation> Project::findClip(ObjectId clipId) const
{
    for (int trackIndex = 0; trackIndex < getNumTracks(); ++trackIndex)
        if (const int clipIndex = getTrack(trackIndex).indexOfClip(clipId); clipIndex >= 0)
            return ClipLocation{trackIndex, clipIndex};

    return std::nullopt;
}

void Project::moveClip(ObjectId clipId, int toTrackIndex, Ticks newPosition,
                       juce::UndoManager* undoManager)
{
    const auto location = findClip(clipId);
    jassert(location.has_value() && toTrackIndex >= 0 && toTrackIndex < getNumTracks());
    if (!location || toTrackIndex < 0 || toTrackIndex >= getNumTracks())
        return;

    auto fromTrack = state.getChild(location->trackIndex);
    auto clipState = fromTrack.getChild(location->clipIndex);

    // Removing and appending also brings the clip to the top on its own track.
    fromTrack.removeChild(location->clipIndex, undoManager);
    Clip(clipState).setPosition(newPosition, undoManager);
    state.getChild(toTrackIndex).appendChild(clipState, undoManager);
}

void Project::removeClip(ObjectId clipId, juce::UndoManager* undoManager)
{
    if (const auto location = findClip(clipId))
        state.getChild(location->trackIndex).removeChild(location->clipIndex, undoManager);
}

Ticks Project::getEnd() const
{
    Ticks end = 0;

    for (int trackIndex = 0; trackIndex < getNumTracks(); ++trackIndex)
    {
        const auto track = getTrack(trackIndex);
        for (int clipIndex = 0; clipIndex < track.getNumClips(); ++clipIndex)
        {
            const auto clip = track.getClip(clipIndex);
            end = std::max(end, clip.getEnd().value_or(clip.getPosition()));
        }
    }

    return end;
}

TempoMap Project::getTempoMap() const
{
    return TempoMap(static_cast<double>(state[ids::tempo]),
                    {static_cast<int>(state[ids::timeSignatureNumerator]),
                     static_cast<int>(state[ids::timeSignatureDenominator])});
}

void Project::setTempo(double quarterNotesPerMinute, juce::UndoManager* undoManager)
{
    const TempoMap clamped(quarterNotesPerMinute, getTempoMap().getTimeSignature());
    state.setProperty(ids::tempo, clamped.getTempo(), undoManager);
}

void Project::setTimeSignature(TimeSignature timeSignature, juce::UndoManager* undoManager)
{
    jassert(timeSignature.isValid());
    if (!timeSignature.isValid())
        return;

    state.setProperty(ids::timeSignatureNumerator, timeSignature.numerator, undoManager);
    state.setProperty(ids::timeSignatureDenominator, timeSignature.denominator, undoManager);
}

bool Project::isLoopEnabled() const
{
    return static_cast<bool>(state[ids::loopEnabled]);
}

void Project::setLoopEnabled(bool shouldLoop)
{
    state.setProperty(ids::loopEnabled, shouldLoop, nullptr);
}

Ticks Project::getLoopStart() const
{
    return static_cast<Ticks>(state[ids::loopStart]);
}

Ticks Project::getLoopEnd() const
{
    return static_cast<Ticks>(state[ids::loopEnd]);
}

void Project::setLoopRange(Ticks start, Ticks end, juce::UndoManager* undoManager)
{
    start = std::max(Ticks{0}, start);
    end = std::max(Ticks{0}, end);
    if (end < start)
        std::swap(start, end);

    state.setProperty(ids::loopStart, start, undoManager);
    state.setProperty(ids::loopEnd, end, undoManager);
}

bool Project::isSnapEnabled() const
{
    return static_cast<bool>(state[ids::snapEnabled]);
}

void Project::setSnapEnabled(bool shouldSnap)
{
    state.setProperty(ids::snapEnabled, shouldSnap, nullptr);
}

GridDivision Project::getGridDivision() const
{
    return gridDivisionFromString(state[ids::gridDivision].toString())
        .value_or(GridDivision::automatic);
}

void Project::setGridDivision(GridDivision division)
{
    state.setProperty(ids::gridDivision, toString(division), nullptr);
}

double Project::getMasterVolume() const
{
    return static_cast<double>(state.getProperty(ids::masterVolume, 1.0));
}

void Project::setMasterVolume(double gain, juce::UndoManager* undoManager)
{
    state.setProperty(ids::masterVolume,
                      std::isfinite(gain) ? std::clamp(gain, 0.0, Track::maximumVolume) : 1.0,
                      undoManager);
}

bool Project::isEquivalentTo(const Project& other) const
{
    return state.isEquivalentTo(other.state);
}

} // namespace daw::model
