#include "daw/model/Send.h"

#include "daw/model/Track.h"

#include "Identifiers.h"

#include <algorithm>
#include <cmath>

namespace daw::model
{

Send::Send(juce::ValueTree sendState)
    : state(std::move(sendState))
{
    jassert(state.hasType(ids::send));
}

ObjectId Send::getId() const
{
    return static_cast<ObjectId>(state[ids::id]);
}

ObjectId Send::getBus() const
{
    return static_cast<ObjectId>(state[ids::busId]);
}

double Send::getLevel() const
{
    return static_cast<double>(state.getProperty(ids::level, 1.0));
}

void Send::setLevel(double gain, juce::UndoManager* undoManager)
{
    state.setProperty(ids::level,
                      std::isfinite(gain) ? std::clamp(gain, 0.0, Track::maximumVolume) : 1.0,
                      undoManager);
}

bool Send::isPreFader() const
{
    return static_cast<bool>(state[ids::preFader]);
}

void Send::setPreFader(bool shouldBePreFader, juce::UndoManager* undoManager)
{
    state.setProperty(ids::preFader, shouldBePreFader, undoManager);
}

} // namespace daw::model
