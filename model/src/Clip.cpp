#include "daw/model/Clip.h"

#include "Identifiers.h"

#include <algorithm>

namespace daw::model
{

Clip::Clip(juce::ValueTree clipState)
    : state(std::move(clipState))
{
    jassert(state.hasType(ids::clip));
}

ObjectId Clip::getId() const
{
    return static_cast<ObjectId>(state[ids::id]);
}

juce::File Clip::getAudioFile() const
{
    const auto path = state[ids::audioFile].toString();
    return path.isEmpty() ? juce::File() : juce::File(path);
}

void Clip::setAudioFile(const juce::File& file, juce::UndoManager* undoManager)
{
    state.setProperty(ids::audioFile, file.getFullPathName(), undoManager);
}

ClipBounds Clip::getBounds() const
{
    return {getPosition(), getOffset(), getLength()};
}

Ticks Clip::getPosition() const
{
    return static_cast<Ticks>(state[ids::position]);
}

Ticks Clip::getOffset() const
{
    return static_cast<Ticks>(state[ids::offset]);
}

std::optional<Ticks> Clip::getLength() const
{
    if (!state.hasProperty(ids::length))
        return std::nullopt;

    return static_cast<Ticks>(state[ids::length]);
}

std::optional<Ticks> Clip::getEnd() const
{
    if (const auto length = getLength())
        return getPosition() + *length;

    return std::nullopt;
}

void Clip::setBounds(const ClipBounds& bounds, juce::UndoManager* undoManager)
{
    state.setProperty(ids::position, std::max(Ticks{0}, bounds.position), undoManager);
    state.setProperty(ids::offset, std::max(Ticks{0}, bounds.offset), undoManager);

    if (bounds.length)
        state.setProperty(ids::length, std::max(minimumLength, *bounds.length), undoManager);
    else
        state.removeProperty(ids::length, undoManager);
}

void Clip::setPosition(Ticks position, juce::UndoManager* undoManager)
{
    state.setProperty(ids::position, std::max(Ticks{0}, position), undoManager);
}

void Clip::resolveLength(Ticks fileLength)
{
    if (state.hasProperty(ids::length))
        return;

    state.setProperty(ids::length, std::max(minimumLength, fileLength - getOffset()), nullptr);
}

} // namespace daw::model
