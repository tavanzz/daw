#include "daw/model/TempoMap.h"

#include <algorithm>
#include <cmath>

namespace daw::model
{

bool TimeSignature::isValid() const noexcept
{
    const bool powerOfTwo = denominator > 0 && (denominator & (denominator - 1)) == 0;
    return numerator >= 1 && numerator <= maximumNumerator && powerOfTwo &&
           denominator <= maximumDenominator;
}

TempoMap::TempoMap(double quarterNotesPerMinute, TimeSignature timeSignature) noexcept
    : tempo(std::isfinite(quarterNotesPerMinute)
                ? std::clamp(quarterNotesPerMinute, minimumTempo, maximumTempo)
                : defaultTempo)
    , signature(timeSignature.isValid() ? timeSignature : TimeSignature{})
{
}

double TempoMap::ticksPerQuarterNote() const noexcept
{
    return static_cast<double>(ticksPerSecond) * 60.0 / tempo;
}

double TempoMap::ticksPerBeat() const noexcept
{
    return ticksPerQuarterNote() * 4.0 / signature.denominator;
}

double TempoMap::ticksPerBar() const noexcept
{
    return ticksPerBeat() * signature.numerator;
}

Ticks TempoMap::barStart(juce::int64 bar) const noexcept
{
    return static_cast<Ticks>(std::llround(static_cast<double>(bar) * ticksPerBar()));
}

Ticks TempoMap::beatStart(juce::int64 bar, int beat) const noexcept
{
    return barStart(bar) + static_cast<Ticks>(std::llround(beat * ticksPerBeat()));
}

juce::int64 TempoMap::barAt(Ticks position) const noexcept
{
    // The estimate can be one off where rounding put a bar start a tick away from
    // the exact value; the comparisons settle it against the rounded starts.
    auto bar = static_cast<juce::int64>(std::floor(static_cast<double>(position) / ticksPerBar()));

    while (barStart(bar) > position)
        --bar;
    while (barStart(bar + 1) <= position)
        ++bar;

    return bar;
}

MusicalPosition TempoMap::toMusical(Ticks position) const noexcept
{
    MusicalPosition result;
    result.bar = barAt(position);

    const auto offset = static_cast<double>(position - barStart(result.bar));
    int beat = std::clamp(static_cast<int>(std::floor(offset / ticksPerBeat())), 0,
                          signature.numerator - 1);

    while (beat > 0 && beatStart(result.bar, beat) > position)
        --beat;
    while (beat + 1 < signature.numerator && beatStart(result.bar, beat + 1) <= position)
        ++beat;

    const Ticks start = beatStart(result.bar, beat);
    const Ticks end = beat + 1 < signature.numerator ? beatStart(result.bar, beat + 1)
                                                     : barStart(result.bar + 1);
    result.beat = beat;
    result.beatFraction = end > start ? std::clamp(static_cast<double>(position - start) /
                                                       static_cast<double>(end - start),
                                                   0.0, std::nextafter(1.0, 0.0))
                                      : 0.0;
    return result;
}

juce::String TempoMap::formatBarsBeats(Ticks position) const
{
    const auto musical = toMusical(position);
    const int sixteenthsPerBeat = std::max(1, 16 / signature.denominator);
    const int sixteenth = std::min(sixteenthsPerBeat - 1,
                                   static_cast<int>(musical.beatFraction * sixteenthsPerBeat));

    return juce::String(musical.bar + 1) + "." + juce::String(musical.beat + 1) + "." +
           juce::String(sixteenth + 1);
}

} // namespace daw::model
