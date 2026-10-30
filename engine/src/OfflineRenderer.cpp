#include "daw/engine/OfflineRenderer.h"

#include <algorithm>

namespace daw::engine
{

OfflineRenderer::OfflineRenderer(const Arrangement& arrangementToRender, int blockSize)
    : arrangement(arrangementToRender)
    , state(MixState::startingAt(arrangementToRender))
    , maximumBlockSize(std::max(1, blockSize))
{
    jassert(arrangement.sampleRate > 0.0);
    mixer.prepare(arrangement.sampleRate, maximumBlockSize);
}

void OfflineRenderer::render(juce::AudioBuffer<float>& output)
{
    for (int done = 0; done < output.getNumSamples(); done += maximumBlockSize)
    {
        const int length = std::min(maximumBlockSize, output.getNumSamples() - done);
        mixer.render(arrangement, state, position, output, done, length);
        position += length;
    }
}

} // namespace daw::engine
