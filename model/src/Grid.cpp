#include "daw/model/Grid.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace daw::model
{

namespace
{

// The note value of a division: 1 = whole note, 4 = quarter... 0 for bar.
int noteValue(GridDivision division) noexcept
{
    switch (division)
    {
    case GridDivision::half:
        return 2;
    case GridDivision::quarter:
        return 4;
    case GridDivision::eighth:
        return 8;
    case GridDivision::sixteenth:
        return 16;
    case GridDivision::thirtySecond:
        return 32;
    case GridDivision::automatic:
    case GridDivision::bar:
        break;
    }

    return 0;
}

} // namespace

Grid::Grid(const TempoMap& map, GridDivision gridDivision) noexcept
    : tempoMap(map)
    , division(gridDivision == GridDivision::automatic ? GridDivision::bar : gridDivision)
    , spacing(divisionTicks(map, division))
{
    jassert(gridDivision != GridDivision::automatic);

    // Lines per bar from the exact fraction numerator * value / denominator, rounded
    // up: no floating-point doubt about whether the last line equals the bar line.
    const auto signature = map.getTimeSignature();
    if (const int value = noteValue(division); value > 0)
        linesPerBar =
            (signature.numerator * value + signature.denominator - 1) / signature.denominator;
}

GridDivision Grid::automaticDivision(const TempoMap& map, double ticksPerPixel,
                                     double minimumSpacingPixels) noexcept
{
    constexpr std::array finestFirst{GridDivision::thirtySecond, GridDivision::sixteenth,
                                     GridDivision::eighth,       GridDivision::quarter,
                                     GridDivision::half,         GridDivision::bar};

    for (const auto candidate : finestFirst)
        if (divisionTicks(map, candidate) / ticksPerPixel >= minimumSpacingPixels)
            return candidate;

    return GridDivision::bar;
}

double Grid::divisionTicks(const TempoMap& map, GridDivision gridDivision) noexcept
{
    const int value = noteValue(gridDivision);
    return value > 0 ? map.ticksPerQuarterNote() * 4.0 / value : map.ticksPerBar();
}

Ticks Grid::snap(Ticks position) const noexcept
{
    const Ticks before = lineAtOrBefore(position);
    if (before == position)
        return position;

    const Ticks after = lineAfter(position);
    return after - position < position - before ? after : before;
}

Ticks Grid::lineAtOrBefore(Ticks position) const noexcept
{
    const auto bar = tempoMap.barAt(position);
    const Ticks offset = position - tempoMap.barStart(bar);
    int index = std::clamp(static_cast<int>(std::floor(static_cast<double>(offset) / spacing)), 0,
                           linesPerBar - 1);

    while (index > 0 && line(bar, index) > position)
        --index;
    while (index + 1 < linesPerBar && line(bar, index + 1) <= position)
        ++index;

    return line(bar, index);
}

Ticks Grid::lineAfter(Ticks position) const noexcept
{
    const auto bar = tempoMap.barAt(position);

    for (int index = 0; index < linesPerBar; ++index)
        if (const Ticks candidate = line(bar, index); candidate > position)
            return candidate;

    return tempoMap.barStart(bar + 1);
}

std::vector<Ticks> Grid::linesBetween(Ticks start, Ticks end) const
{
    std::vector<Ticks> lines;

    for (Ticks position = lineAtOrBefore(start); position < end; position = lineAfter(position))
        if (position >= start)
            lines.push_back(position);

    return lines;
}

Ticks Grid::line(juce::int64 bar, int index) const noexcept
{
    if (index == 0)
        return tempoMap.barStart(bar);

    return tempoMap.barStart(bar) + static_cast<Ticks>(std::llround(index * spacing));
}

} // namespace daw::model
