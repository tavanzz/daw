#pragma once

#include <juce_core/juce_core.h>

namespace daw::engine
{

/**
    Where the beats fall, in samples at the arrangement's rate.

    The rule belongs to the project's tempo map, which the engine does not know: the
    controller implements this interface on top of it, so the engine places clicks
    exactly where the model puts its beats without repeating its rounding.

    Beats are numbered across bars: beat 0 is the first beat of the first bar, at the
    start of the timeline; negative beats lie before it (a count-in from the start).

    Created on the message thread and shared, immutable, with the audio thread:
    implementations must be real-time safe (plain arithmetic).
*/
class BeatGrid
{
public:
    virtual ~BeatGrid() = default;

    /** Audio thread. Sample position of a beat. Increases with beat. */
    [[nodiscard]] virtual juce::int64 beatPosition(juce::int64 beat) const noexcept = 0;

    /** Beats per bar: beat b is the first of its bar when b is a multiple of it. */
    [[nodiscard]] virtual int beatsPerBar() const noexcept = 0;

    /** Roughly the length of a beat in samples: where to start looking for one. */
    [[nodiscard]] virtual double approximateBeatLength() const noexcept = 0;

    /** Audio thread. The first beat at or after position. */
    [[nodiscard]] juce::int64 firstBeatFrom(juce::int64 position) const noexcept;

    /** Audio thread. True for the first beat of a bar. */
    [[nodiscard]] bool isDownbeat(juce::int64 beat) const noexcept;
};

} // namespace daw::engine
