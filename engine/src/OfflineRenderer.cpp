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
        history.reset(readPosition, readPosition);
        history.record({readPosition, readPosition, TimelineHistory::Kind::playing, false});
        discarded.setSize(std::max(2, output.getNumChannels()), maximumBlockSize);
        for (int done = 0; done < state.latency;)
        {
            const int length = std::min(maximumBlockSize, state.latency - done);
            renderRead(discarded, 0, length);
            done += length;
        }
    }

    for (int done = 0; done < output.getNumSamples();)
    {
        const int length = std::min(maximumBlockSize, output.getNumSamples() - done);
        renderRead(output, done, length);
        position += length;
        done += length;
    }
}

void OfflineRenderer::renderRead(juce::AudioBuffer<float>& output, int start, int length)
{
    for (int done = 0; done < length;)
    {
        // The timeline stops at its end: what is read there on is silence.
        const bool running = readPosition < timelineEnd;
        int piece = length - done;
        if (running)
            piece = static_cast<int>(std::min<juce::int64>(piece, timelineEnd - readPosition));
        else
            history.record({readPosition, timelineEnd, TimelineHistory::Kind::stopped, false});

        auto context = contextAt();
        context.timelineRunning = running;
        mixer.render(arrangement, state, running ? readPosition : timelineEnd, output, start + done,
                     piece, context);
        readPosition += piece;
        done += piece;
    }
}

MixContext OfflineRenderer::contextAt() const noexcept
{
    MixContext context;
    context.history = &history;
    context.stream = readPosition;
    return context;
}

} // namespace daw::engine
