#pragma once

#include "daw/model/TimelineTime.h"

#include <juce_core/juce_core.h>

namespace daw::model
{

/** A metre: numerator beats of one 1/denominator note per bar. */
struct TimeSignature
{
    static constexpr int maximumNumerator = 32;
    static constexpr int maximumDenominator = 32;

    int numerator = 4;
    int denominator = 4;

    /** 1 to 32 beats of a whole, half, quarter, eighth, sixteenth or 32nd note. */
    [[nodiscard]] bool isValid() const noexcept;

    bool operator==(const TimeSignature&) const = default;
};

/** A place in bars and beats. Everything counts from 0: the first beat of the
    project is bar 0, beat 0 (shown as 1.1). */
struct MusicalPosition
{
    juce::int64 bar = 0;
    int beat = 0;             ///< beat within the bar: 0 to numerator - 1
    double beatFraction = 0.0; ///< how far into the beat, 0 (inclusive) to 1 (exclusive)
};

/**
    Converts between ticks and bars and beats for one tempo and one metre, fixed for
    the whole project.

    The tempo counts quarter notes per minute whatever the metre, as most DAWs do:
    at 120, a 4/4 bar lasts 2 s and a 6/8 bar 1.5 s. A beat is one 1/denominator
    note (an eighth in 6/8).

    Bar and beat starts are rounded to whole ticks once, here, and every conversion
    goes through them, so a position on a bar line always reads back as that bar,
    whatever the tempo. Plain value type, any thread.
*/
class TempoMap
{
public:
    static constexpr double minimumTempo = 20.0;
    static constexpr double maximumTempo = 999.0;
    static constexpr double defaultTempo = 120.0;

    /** 120 quarter notes per minute, 4/4. */
    TempoMap() = default;

    /** The tempo is clamped to [minimumTempo, maximumTempo]; an invalid time
        signature becomes 4/4. */
    TempoMap(double quarterNotesPerMinute, TimeSignature timeSignature) noexcept;

    [[nodiscard]] double getTempo() const noexcept { return tempo; }
    [[nodiscard]] TimeSignature getTimeSignature() const noexcept { return signature; }

    [[nodiscard]] double ticksPerQuarterNote() const noexcept;
    [[nodiscard]] double ticksPerBeat() const noexcept;
    [[nodiscard]] double ticksPerBar() const noexcept;

    /** Where a bar (0 = the first) starts. */
    [[nodiscard]] Ticks barStart(juce::int64 bar) const noexcept;

    /** Where a beat of a bar starts (beat 0 is the bar start). */
    [[nodiscard]] Ticks beatStart(juce::int64 bar, int beat) const noexcept;

    /** The bar that contains position: barStart(bar) <= position < barStart(bar + 1). */
    [[nodiscard]] juce::int64 barAt(Ticks position) const noexcept;

    [[nodiscard]] MusicalPosition toMusical(Ticks position) const noexcept;

    /** "bar.beat.sixteenth", counting from 1 as musicians do: "1.1.1" is the start.
        The last field counts sixteenth notes within the beat (always 1 when the beat
        is a sixteenth or shorter). */
    [[nodiscard]] juce::String formatBarsBeats(Ticks position) const;

    bool operator==(const TempoMap&) const = default;

private:
    double tempo = defaultTempo;
    TimeSignature signature;
};

} // namespace daw::model
