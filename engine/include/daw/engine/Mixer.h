#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/AudioInputs.h"
#include "daw/engine/NotePlayback.h"
#include "daw/engine/TimelineFade.h"
#include "daw/engine/TimelineHistory.h"
#include "daw/engine/dsp/DelayLine.h"
#include "daw/engine/dsp/GainRamp.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

namespace daw::engine
{

/** The gains the mixer is moving for one arrangement: per track, send, bus and for
    the master; where the buses sum their sends; and the delays that keep every path
    aligned (latency compensation). Allocated with the arrangement on the message
    thread; only the thread that renders that arrangement touches it afterwards.

    Latency compensation: a track's inserts delay it by their latency L(t). A bus
    gets its sends aligned to the slowest track sending to it, A(b), and its own
    inserts add L(b). Every track and bus reaches the master aligned to the slowest
    of them all, M; the master's inserts add their latency. So each send is delayed
    by A(b) - L(t), each track by M - L(t) and each bus by M - A(b) - L(b), and the
    output lags the timeline read by latency = M + L(master).
*/
struct MixState
{
    /** How many samples the buses can sum at once unless asked otherwise; the mixer
        renders longer stretches in pieces. */
    static constexpr int defaultBusBlockSize = 4096;

    struct TrackGains
    {
        dsp::GainRamp left;
        dsp::GainRamp right;

        // What the track plays: its timeline (clips) and its input, crossfaded when
        // monitoring switches; one is at 1 when the other is at 0.
        dsp::GainRamp timeline;
        dsp::GainRamp input{0.0f};

        // From the fixed gains (0) to automation (1), when automation starts.
        dsp::GainRamp automationBlend{0.0f};

        struct Send
        {
            dsp::GainRamp gain;
            dsp::GainRamp automationBlend{0.0f};
            int delay = 0;
            dsp::DelayLine line;
        };
        std::vector<Send> sends; ///< one per send, same order

        int latency = 0;     ///< of the inserts
        int outputDelay = 0; ///< to reach the master aligned
        dsp::DelayLine output;

        /** A MIDI track's notes from the timeline (see NotePlayback), from its live
            input and from previews (see midi::LiveNotes); null for audio tracks. */
        std::unique_ptr<NotePlayback> notes;
        std::unique_ptr<midi::LiveNotes> live;
        std::unique_ptr<midi::LiveNotes> previews;
    };

    struct BusGains
    {
        dsp::GainRamp left;
        dsp::GainRamp right;
        dsp::GainRamp automationBlend{0.0f};

        int inputLatency = 0; ///< what its sends are aligned to: A(b)
        int outputDelay = 0;
        dsp::DelayLine output;
    };

    std::vector<TrackGains> tracks; ///< one per track of the arrangement, same order
    std::vector<BusGains> buses;    ///< one per bus, same order
    dsp::GainRamp master;
    dsp::GainRamp masterAutomationBlend{0.0f};

    /** Two channels per bus: what its sends add up to in the stretch being mixed. */
    juce::AudioBuffer<float> busInputs;

    int mixLatency = 0; ///< M: where tracks and buses meet at the master
    int latency = 0;    ///< how far the output lags the timeline read

    /** Rendering thread. Panic: every track forgets the notes it holds (timeline,
        live, previews); their instruments were reset. */
    void forgetNotes() noexcept;

    /** Message thread. Gains already at the arrangement's targets (no ramps, and
        automation that is there already applies from the first sample); buses sum up
        to busBlockSize samples at once. */
    [[nodiscard]] static MixState startingAt(const Arrangement& arrangement,
                                             int busBlockSize = defaultBusBlockSize);

    /** Rendering thread. Takes over the gains and delay lines of the tracks, sends
        and buses that previous also had (matched by id) and ramps the gains to
        arrangement's targets over rampLength samples, so that a change of
        arrangement never makes a gain jump or loses delayed audio. New ones start
        at their targets. Allocates nothing. */
    void continueFrom(MixState& previous, const Arrangement& previousArrangement,
                      const Arrangement& arrangement, int rampLength) noexcept;

    /** Any thread. Whether going from previous to this state moves audio in time:
        the total latency, or the latency or a delay of a track, send or bus that
        both have, differs. (New paths start from silence; paths that are gone just
        end.) */
    [[nodiscard]] bool movesAudioFrom(const MixState& previous,
                                      const Arrangement& previousArrangement,
                                      const Arrangement& arrangement) const noexcept;

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

    /** Live MIDI of the current audio block (offsets from its start, as
        inputOffset), from devices and previews; empty offline. */
    midi::LiveEventSpan live;

    /** Live MIDI was not heard for a while (the engine skipped rendering): MIDI
        tracks end the live notes they hold, whose note-offs may have been missed. */
    bool liveRestarts = false;

    /** What was read when (see TimelineHistory), and the stream index of the
        stretch's first sample: each point of the mix that lags by some latency
        takes its automation from the timeline position it hears. Null: the timeline
        runs on in a straight line from timelineStart. */
    const TimelineHistory* history = nullptr;
    juce::int64 stream = 0;

    /** While the engine changes from one arrangement to the next: the one before and
        its gains. Tracks whose clips changed crossfade their clips (before their
        effects) from the old ones to the new over crossfadeLength samples, of which
        crossfadeDone are done; tracks the new arrangement no longer has fade out. */
    const Arrangement* previous = nullptr;
    MixState* previousState = nullptr;
    int crossfadeDone = 0;
    int crossfadeLength = 1;
};

/**
    Mixes an arrangement: reads the clips of every track, applies their fades, the
    track's effects, the track gains (volume, pan, mute and solo, ramped) and the
    master gain and effects, and sums the tracks into a stereo output.

    A track that monitors plays its input instead of its clips, through the same
    gains and meter; the switch is a monitorRampSeconds crossfade. Mono inputs are
    heard on both sides.

    Sends add a track's audio, after its inserts (pre-fader) or after its volume and
    pan (post-fader), to an aux bus; each bus runs its sum through its effects and
    its fader into the mix, before the master.

    Automation moves volumes, pans, send levels and effect parameters sample by
    sample. Each is read at the timeline position of the audio where it applies:
    after effects with latency, that is what was read that many samples before. A
    curve that starts (or comes back, as a track is unmuted) is reached over the
    gain ramp's time; one that stops leaves the fixed gain ramping from its last
    value.

    The same code renders real-time playback and offline exports, and its result
    does not depend on how the timeline is cut into blocks: every gain is a function
    of the sample's position, so exports equal playback sample for sample.

    A MIDI track plays notes and controllers (see NotePlayback) through the
    instrument first in its chain, at their samples; the transport's fades do not
    apply to it: its notes end where playback stops, pauses or fades out, and the
    instrument releases them.

    Effects run whenever the mixer renders, also while the timeline is stopped, so
    their tails ring on. A track without effects that is silent is skipped, as it
    always was; with effects it is processed (its tail may still sound) but only
    added to the mix when its gains let it be heard.

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
    void renderPiece(const Arrangement& arrangement, MixState& state, juce::int64 timelineStart,
                     float* const* mix, int numSamples, const MixContext& context) noexcept;

    /** Leaves the track's output in scratch and adds its sends to the buses of
        busOwner (whose state holds them); false if it adds nothing to the mix. A
        vanishing track fades out (see MixContext::previous). */
    bool renderTrack(const Arrangement& arrangement, const ArrangementTrack& track,
                     MixState::TrackGains& gains, juce::int64 timelineStart, int numSamples,
                     const MixContext& context, const ArrangementTrack* before,
                     const Arrangement& busOwner, MixState& busState, bool vanishing) noexcept;
    void addSends(const ArrangementTrack& track, MixState::TrackGains& gains, bool preFader,
                  int numSamples, const Arrangement& busOwner, MixState& busState,
                  const MixContext& context, juce::int64 timelineStart) noexcept;
    void renderBuses(const Arrangement& arrangement, MixState& state, juce::int64 timelineStart,
                     float* const* mix, int numSamples, const MixContext& context) noexcept;
    void renderVanishedTracks(const Arrangement& arrangement, MixState& state,
                              juce::int64 timelineStart, float* const* mix, int numSamples,
                              const MixContext& context) noexcept;
    void readClips(const ArrangementTrack& track, juce::int64 timelineStart, int numSamples,
                   juce::AudioBuffer<float>& destination) noexcept;
    void crossfadeClips(const ArrangementTrack* before, juce::int64 timelineStart,
                        int numSamples, const MixContext& context) noexcept;
    void readInput(const ArrangementTrack& track, const MixContext& context,
                   int numSamples) noexcept;
    void processChain(const EffectChain& chain, const Arrangement& arrangement,
                      float* const* channels, int numSamples, juce::int64 timelineStart,
                      const MixContext& context, int latency,
                      midi::EventSpan events = {}) noexcept;
    void gatherNotes(const ArrangementTrack& track, MixState::TrackGains& gains,
                     juce::int64 timelineStart, int numSamples, const MixContext& context,
                     bool vanishing) noexcept;

    /** Calls callback(offset, length, position, running) for each stretch of the
        timeline heard at a point lagging latency samples behind what is read. */
    template <typename Callback>
    void forEachPosition(const MixContext& context, juce::int64 timelineStart, int latency,
                         int numSamples, Callback&& callback) const noexcept;
    /** Where the slot's automation may step in this stretch (curve points, timeline
        jumps) into slotBreakpoints; returns how many. */
    int findBreakpoints(const ChainSlot& slot, const MixContext& context, juce::int64 timelineStart,
                        int latency, int numSamples) noexcept;
    void fillCurve(const AutomationCurve& curve, const MixContext& context,
                   juce::int64 timelineStart, int latency, int numSamples,
                   float* values) const noexcept;
    void applyPannedGains(const GainAutomation& automation, dsp::GainRamp& left,
                          dsp::GainRamp& right, dsp::GainRamp& blend, float* const* channels,
                          int numSamples, const MixContext& context, juce::int64 timelineStart,
                          int latency) noexcept;
    void applyAutomatedGain(const AutomationCurve* curve, dsp::GainRamp& gain,
                            dsp::GainRamp& blend, float* const* channels, int numChannels,
                            int numSamples, const MixContext& context,
                            juce::int64 timelineStart, int latency) noexcept;

    int gainRampLength = 1;
    int monitorRampLength = 1;
    juce::AudioBuffer<float> scratch;      // one track at a time
    juce::AudioBuffer<float> inputScratch; // its input, while crossfading
    juce::AudioBuffer<float> oldClips;     // its previous clips, while crossfading
    juce::AudioBuffer<float> sendScratch;  // one send at a time
    std::vector<float> gainScratch;
    std::vector<float> leftGains;      // per sample, while automated
    std::vector<float> rightGains;
    std::vector<float> curveScratch;
    std::vector<float> automationLanes; // effect parameters' automation, one lane each
    std::vector<const float*> automationPointers;
    std::array<int, fx::EffectScratch::maximumBreakpoints> slotBreakpoints{}; // one slot's
    fx::EffectScratch effectScratch;
    midi::EventBuffer noteEvents; // one MIDI track's, for its instrument
};

} // namespace daw::engine
