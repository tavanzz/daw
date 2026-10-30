#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/dsp/GainRamp.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace daw::engine
{

/** The gains the mixer is moving for one arrangement: per track and for the master.
    Allocated with the arrangement on the message thread; only the thread that
    renders that arrangement touches it afterwards. */
struct MixState
{
    struct TrackGains
    {
        dsp::GainRamp left;
        dsp::GainRamp right;
    };

    std::vector<TrackGains> tracks; ///< one per track of the arrangement, same order
    dsp::GainRamp master;

    /** Message thread. Gains already at the arrangement's targets (no ramps). */
    [[nodiscard]] static MixState startingAt(const Arrangement& arrangement);

    /** Rendering thread. Takes over the gains of the tracks that previous also had
        (matched by id) and ramps them to arrangement's targets over rampLength
        samples, so that a change of arrangement never makes a gain jump. New
        tracks start at their targets. Allocates nothing. */
    void continueFrom(const MixState& previous, const Arrangement& previousArrangement,
                      const Arrangement& arrangement, int rampLength) noexcept;
};

/**
    Mixes an arrangement: reads the clips of every track, applies their fades, the
    track gains (volume, pan, mute and solo, ramped) and the master gain, and sums
    the tracks into a stereo output.

    The same code renders real-time playback and offline exports, and its result
    does not depend on how the timeline is cut into blocks: every gain is a function
    of the sample's position, so exports equal playback sample for sample.

    render() is real-time safe. prepare() allocates.
*/
class Mixer
{
public:
    static constexpr double gainRampSeconds = 0.02;

    /** Not real-time safe. */
    void prepare(double sampleRate, int maximumBlockSize);

    [[nodiscard]] int getGainRampLength() const noexcept { return gainRampLength; }
    [[nodiscard]] int getMaximumBlockSize() const noexcept { return scratch.getNumSamples(); }

    /** Audio thread (or the exporting thread). Overwrites samples [outputStart,
        outputStart + numSamples) of output's first two channels with the mix of
        timeline samples [timelineStart, timelineStart + numSamples); clears any
        further channels. numSamples must not exceed the prepared block size; state
        must belong to arrangement. Updates the track meters. */
    void render(const Arrangement& arrangement, MixState& state, juce::int64 timelineStart,
                juce::AudioBuffer<float>& output, int outputStart, int numSamples) noexcept;

private:
    void renderTrack(const ArrangementTrack& track, MixState::TrackGains& gains,
                     juce::int64 timelineStart, int numSamples) noexcept;

    int gainRampLength = 1;
    juce::AudioBuffer<float> scratch; // one track at a time
};

} // namespace daw::engine
