#pragma once

#include "daw/engine/AudioSource.h"
#include "daw/engine/BeatGrid.h"
#include "daw/engine/dsp/PeakLevelMeter.h"
#include "daw/engine/fx/EffectInstance.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace daw::engine
{

/** A stretch of a clip that is heard, in samples at the arrangement's rate. */
struct ClipSegment
{
    const AudioSource* source = nullptr;
    juce::int64 start = 0;       ///< timeline position of the first sample
    juce::int64 end = 0;         ///< timeline position just after the last sample
    juce::int64 sourceStart = 0; ///< the source sample played at start
    int fadeInLength = 0;        ///< samples fading in from start (on cut edges only)
    int fadeOutLength = 0;       ///< samples fading out before end (on cut edges only)

    bool operator==(const ClipSegment&) const = default;
};

/** When a track plays its input instead of its clips (see model::MonitoringMode). */
enum class InputMonitoring : std::uint8_t
{
    off,
    automatic, ///< while armed and stopped, during the count-in and while recording
    on
};

/** One effect of a chain, as an arrangement plays it. */
struct ChainSlot
{
    /** The effect. One instance per effect, shared by every arrangement that plays
        it, so that its state (tails, envelopes) goes on when the arrangement
        changes; never in two places of one arrangement. */
    std::shared_ptr<fx::EffectInstance> instance;

    std::vector<float> parameters; ///< normalised values set by the user, one per parameter
    bool enabled = true;           ///< false: bypassed, or fading out before it is removed
    std::uint32_t revival = 0;     ///< see fx::SlotSettings
};

/** Effects in series, processed in order. */
struct EffectChain
{
    std::vector<ChainSlot> slots;

    [[nodiscard]] bool isEmpty() const noexcept { return slots.empty(); }

    /** How many samples the chain delays its audio: the sum of its effects'
        latencies, bypassed ones included (bypassing never moves audio in time). */
    [[nodiscard]] int getLatency() const noexcept;
};

/** A track as the mixer sees it. */
struct ArrangementTrack
{
    juce::int64 id = 0;    ///< the model's track id: keeps gain ramps going across arrangements
    float leftGain = 1.0f; ///< volume × pan law, 0 when the track is not heard
    float rightGain = 1.0f;
    std::vector<ClipSegment> segments; ///< sorted by start, never overlapping

    /** Effects on the track, before its fader. */
    EffectChain inserts;

    int inputChannel = -1;  ///< first device input channel; -1 for none
    int numInputChannels = 0; ///< 1 (heard on both sides) or 2
    InputMonitoring monitoring = InputMonitoring::off;
    bool armed = false;

    /** Measures the track after its fader, for the UI; may be null. */
    std::shared_ptr<dsp::PeakLevelMeter> meter;

    /** Summary of the segments (see Arrangement::layoutHash). */
    std::uint64_t layoutHash = 0;
};

/**
    Everything the audio thread needs to play the project: an immutable snapshot,
    built on the message thread from the model and swapped in as a whole.

    Positions are samples at sampleRate. The arrangement owns (shares) every source
    its segments read, so they stay alive as long as it does; it is always destroyed
    off the audio thread.
*/
struct Arrangement
{
    double sampleRate = 0.0;
    std::vector<ArrangementTrack> tracks;
    float masterGain = 1.0f;

    /** Effects on the mix, after the master fader (so a limiter there holds its
        ceiling whatever the fader does). */
    EffectChain masterInserts;

    /** The project's tempo and metre, for tempo-synced effects. */
    double tempo = 120.0; ///< quarter notes per minute
    int numerator = 4;
    int denominator = 4;

    juce::int64 length = 0;    ///< where the last clip ends
    juce::int64 loopStart = 0; ///< loop region [loopStart, loopEnd); empty = [0, length)
    juce::int64 loopEnd = 0;

    std::vector<std::shared_ptr<const AudioSource>> sources;

    /** Where the beats are, for the metronome and the count-in; may be null. */
    std::shared_ptr<const BeatGrid> beats;
    bool metronomeEnabled = false; ///< clicks while playing (the count-in always clicks)
    float metronomeGain = 0.5f;

    /** Summary of the segments of every track. Two arrangements with different
        hashes sound different even at equal gains: swapping them while playing
        needs a crossfade (of the tracks whose own hashes differ). Computed by
        updateLayoutHash(), with the tracks' own. */
    std::uint64_t layoutHash = 0;

    void updateLayoutHash() noexcept;

    /** Whether any track or the master has effects. */
    [[nodiscard]] bool hasEffects() const noexcept;
};

/** Audio thread (or any). Whether every source the arrangement plays in
    [position, position + length) can deliver it now (streamed files may still be
    loading). */
[[nodiscard]] bool isArrangementReady(const Arrangement& arrangement, juce::int64 position,
                                      juce::int64 length) noexcept;

/** A clip placed on a track, in samples, before overlaps are resolved. */
struct ClipPlacement
{
    std::shared_ptr<const AudioSource> source; ///< null for a clip whose file is not loaded
    juce::int64 start = 0;
    juce::int64 end = 0;
    juce::int64 sourceStart = 0;
};

/** Length of the fade on clip edges that cut into the audio. */
inline constexpr double clipEdgeFadeSeconds = 0.005;

/**
    Resolves overlaps between the clips of one track.

    clips are in stacking order, the last on top: a clip hides whatever it covers
    below it (a clip without a source hides it too, and plays silence). Returns what
    is heard, sorted by start. Edges that cut into the audio (where a clip was
    trimmed, or where another clip covers it) get a fade of fadeLength samples; the
    natural start and end of a file never do, so an untrimmed file plays exactly as
    it is. Message thread: allocates.
*/
[[nodiscard]] std::vector<ClipSegment> flattenClips(std::span<const ClipPlacement> clips,
                                                    int fadeLength);

} // namespace daw::engine
