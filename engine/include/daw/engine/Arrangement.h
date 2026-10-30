#pragma once

#include "daw/engine/AudioSource.h"
#include "daw/engine/dsp/PeakLevelMeter.h"

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

/** A track as the mixer sees it. */
struct ArrangementTrack
{
    juce::int64 id = 0;    ///< the model's track id: keeps gain ramps going across arrangements
    float leftGain = 1.0f; ///< volume × pan law, 0 when the track is not heard
    float rightGain = 1.0f;
    std::vector<ClipSegment> segments; ///< sorted by start, never overlapping

    /** Measures the track after its fader, for the UI; may be null. */
    std::shared_ptr<dsp::PeakLevelMeter> meter;
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

    juce::int64 length = 0;    ///< where the last clip ends
    juce::int64 loopStart = 0; ///< loop region [loopStart, loopEnd); empty = [0, length)
    juce::int64 loopEnd = 0;

    std::vector<std::shared_ptr<const AudioSource>> sources;

    /** Summary of the segments of every track. Two arrangements with different
        hashes sound different even at equal gains: swapping them while playing
        needs a crossfade. Computed by updateLayoutHash(). */
    std::uint64_t layoutHash = 0;

    void updateLayoutHash() noexcept;
};

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
