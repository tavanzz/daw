#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/Mixer.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace daw::engine
{

/**
    Renders an arrangement faster than real time, for exports: the same Mixer that
    plays it, so the result equals playback sample for sample (playback adds only
    the transport's fades when it starts, stops or jumps).

    Renders the timeline in order from a start position; any thread, one at a time.
*/
class OfflineRenderer
{
public:
    static constexpr int defaultBlockSize = 4096;

    explicit OfflineRenderer(const Arrangement& arrangementToRender,
                             int blockSize = defaultBlockSize);

    /** Overwrites output (two or more channels) with the next output.getNumSamples()
        samples of the timeline, starting where the previous call ended. */
    void render(juce::AudioBuffer<float>& output);

    /** The timeline position the next render() starts at. */
    [[nodiscard]] juce::int64 getPosition() const noexcept { return position; }
    void setPosition(juce::int64 newPosition) noexcept { position = newPosition; }

private:
    const Arrangement& arrangement;
    Mixer mixer;
    MixState state;
    int maximumBlockSize;
    juce::int64 position = 0;
};

} // namespace daw::engine
