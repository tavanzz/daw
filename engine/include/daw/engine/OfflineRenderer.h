#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/TimelineHistory.h"

#include <juce_audio_basics/juce_audio_basics.h>

namespace daw::engine
{

/**
    Renders an arrangement faster than real time, for exports: the same Mixer that
    plays it, so the result equals playback sample for sample (playback adds only
    the transport's fades when it starts, stops or jumps).

    Renders the timeline in order from a start position; any thread, one at a time.

    Effects with latency delay the mix by that much (see MixState): the renderer reads
    that far ahead and drops what comes out first, so that output sample k is
    timeline position start + k, as heard in playback.
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

    /** The timeline position the next render() starts at. Set it before the first
        render(). */
    [[nodiscard]] juce::int64 getPosition() const noexcept { return position; }
    void setPosition(juce::int64 newPosition) noexcept { position = newPosition; }

    /** How far the mix lags the timeline: what render() makes up for. */
    [[nodiscard]] int getLatency() const noexcept { return state.latency; }

private:
    [[nodiscard]] MixContext contextAt() const noexcept;

    const Arrangement& arrangement;
    Mixer mixer;
    MixState state;
    int maximumBlockSize;
    juce::int64 position = 0;     // of the next sample out
    juce::int64 readPosition = 0; // of the next sample read: latency samples ahead
    bool started = false;
    juce::AudioBuffer<float> discarded;
    TimelineHistory history; // read in a straight line: stream index = read position
};

} // namespace daw::engine
