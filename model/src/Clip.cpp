#include "daw/model/Clip.h"

#include "Identifiers.h"

namespace daw::model
{

Clip::Clip(juce::ValueTree clipState)
    : state(std::move(clipState))
{
    jassert(state.hasType(ids::clip));
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

} // namespace daw::model
