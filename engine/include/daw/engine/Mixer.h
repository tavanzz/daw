#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/AudioInputs.h"
#include "daw/engine/TimelineFade.h"
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

        // What the track plays: its timeline (clips) and its input, crossfaded when
        // monitoring switches; one is at 1 when the other is at 0.
        dsp::GainRamp timeline;
        dsp::GainRamp input{0.0f};
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

    /** True while any track plays some of its input. */
    [[nodiscard]] bool isHearingInput() const noexcept;
};

/** What the mixer needs to know for one stretch of audio besides the arrangement. */
struct MixContext
{
    /** False while the transport is stopped: no clip is read, only inputs can be
        heard. */
    bool timelineRunning = true;

    /** The transport's de-click fade, applied to the clips only. */
    TimelineFade fade;

    /** The device inputs of the current audio block, for monitoring; null when
        rendering offline, which never hears an input. */
    const AudioInputs* inputs = nullptr;
    int inputOffset = 0; ///< where this stretch starts within the inputs' block

    /** Whether tracks monitoring automatically hear their input now (see
        InputMonitoring::automatic). */
    bool autoMonitorsInput = false;
};

/**
    Mixes an arrangement: reads the clips of every track, applies their fades, the
    track gains (volume, pan, mute and solo, ramped) and the master gain, and sums
    the tracks into a stereo output.

    A track that monitors plays its input instead of its clips, through the same
    gains and meter; the switch is a monitorRampSeconds crossfade. Mono inputs are
    heard on both sides.

    The same code renders real-time playback and offline exports, and its result
    does not depend on how the timeline is cut into blocks: every gain is a function
    of the sample's position, so exports equal playback sample for sample.

    render() is real-time safe. prepare() allocates.
*/
class Mixer
{
public:
    static constexpr double gainRampSeconds = 0.02;
    static constexpr double monitorRampSeconds = 0.005;

    /** Not real-time safe. */
    void prepare(double sampleRate, int maximumBlockSize);

    [[nodiscard]] int getGainRampLength() const noexcept { return gainRampLength; }
    [[nodiscard]] int getMaximumBlockSize() const noexcept { return scratch.getNumSamples(); }

    /** Audio thread (or the exporting thread). Overwrites samples [outputStart,
        outputStart + numSamples) of output's first two channels with the mix of
        timeline samples [timelineStart, timelineStart + numSamples) and of the
        monitored inputs (see MixContext); clears any further channels. numSamples
        must not exceed the prepared block size; state must belong to arrangement.
        Updates the track meters. */
    void render(const Arrangement& arrangement, MixState& state, juce::int64 timelineStart,
                juce::AudioBuffer<float>& output, int outputStart, int numSamples,
                const MixContext& context = {}) noexcept;

private:
    void renderTrack(const ArrangementTrack& track, MixState::TrackGains& gains,
                     juce::int64 timelineStart, int numSamples, const MixContext& context) noexcept;
    void readClips(const ArrangementTrack& track, juce::int64 timelineStart,
                   int numSamples) noexcept;
    void readInput(const ArrangementTrack& track, const MixContext& context,
                   int numSamples) noexcept;

    int gainRampLength = 1;
    int monitorRampLength = 1;
    juce::AudioBuffer<float> scratch;      // one track at a time
    juce::AudioBuffer<float> inputScratch; // its input, while crossfading
};

} // namespace daw::engine
