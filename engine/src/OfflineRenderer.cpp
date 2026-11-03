#include "daw/engine/OfflineRenderer.h"

#include <algorithm>

namespace daw::engine
{

OfflineRenderer::OfflineRenderer(const Arrangement& arrangementToRender, int blockSize)
    : arrangement(arrangementToRender)
    , state(MixState::startingAt(arrangementToRender, blockSize))
    , maximumBlockSize(std::max(1, blockSize))
{
    jassert(arrangement.sampleRate > 0.0);
    mixer.prepare(arrangement.sampleRate, maximumBlockSize);
}

void OfflineRenderer::render(juce::AudioBuffer<float>& output)
{
    // The same floating-point mode as the audio thread (see Engine::process()).
    const juce::ScopedNoDenormals noDenormals;

    // The first latency samples out are the delays filling: what comes before the
    // start position.
    if (!started)
    {
        started = true;
        readPosition = position;
        discarded.setSize(std::max(2, output.getNumChannels()), maximumBlockSize);
        for (int done = 0; done < state.latency;)
        {
            const int length = std::min(maximumBlockSize, state.latency - done);
            mixer.render(arrangement, state, readPosition, discarded, 0, length);
            readPosition += length;
            done += length;
        }
    }

    for (int done = 0; done < output.getNumSamples(); done += maximumBlockSize)
    {
        const int length = std::min(maximumBlockSize, output.getNumSamples() - done);
        mixer.render(arrangement, state, readPosition, output, done, length);
        readPosition += length;
        position += length;
    }
}

} // namespace daw::engine
