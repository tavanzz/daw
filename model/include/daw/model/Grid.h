#pragma once

#include "daw/model/TempoMap.h"

#include <cstdint>
#include <optional>
#include <vector>

namespace daw::model
{

/** Spacing of the grid lines that clips snap to. automatic picks one of the others
    from the zoom level (see Grid::automaticDivision()). */
enum class GridDivision : std::uint8_t
{
    automatic,
    bar,
    half,
    quarter,
    eighth,
    sixteenth,
    thirtySecond
};

/** Names of grid divisions as stored in project files and shown in menus: "auto",
    "bar", "1/2", "1/4", "1/8", "1/16", "1/32". */
[[nodiscard]] juce::String toString(GridDivision division);
[[nodiscard]] std::optional<GridDivision> gridDivisionFromString(const juce::String& text);

/**
    The grid lines of a TempoMap at one division, and snapping to them.

    Lines restart at every bar line, so a quarter-note grid in 7/8 has lines at 0,
    1, 2 and 3 quarters, then at the next bar (3.5 quarters). Every line is rounded
    to a whole tick from the bar start, like the TempoMap's beats.

    Plain value type, any thread.
*/
class Grid
{
public:
    /** gridDivision must not be automatic: resolve it with automaticDivision() first. */
    Grid(const TempoMap& map, GridDivision gridDivision) noexcept;

    /** The finest division whose lines are at least minimumSpacingPixels apart at
        this zoom level; bar if even bars are closer than that. */
    [[nodiscard]] static GridDivision automaticDivision(const TempoMap& map, double ticksPerPixel,
                                                        double minimumSpacingPixels) noexcept;

    /** Distance between two lines inside a bar (the last line of a bar may be
        closer to the next bar line). */
    [[nodiscard]] static double divisionTicks(const TempoMap& map,
                                              GridDivision gridDivision) noexcept;

    [[nodiscard]] GridDivision getDivision() const noexcept { return division; }

    /** The nearest line; halfway between two lines goes to the earlier one. */
    [[nodiscard]] Ticks snap(Ticks position) const noexcept;

    /** The last line at or before position. */
    [[nodiscard]] Ticks lineAtOrBefore(Ticks position) const noexcept;

    /** The first line after position. */
    [[nodiscard]] Ticks lineAfter(Ticks position) const noexcept;

    /** Every line in [start, end), in order. Allocates: not for the audio thread. */
    [[nodiscard]] std::vector<Ticks> linesBetween(Ticks start, Ticks end) const;

private:
    [[nodiscard]] Ticks line(juce::int64 bar, int index) const noexcept;

    TempoMap tempoMap;
    GridDivision division;
    double spacing = 0.0;
    int linesPerBar = 1;
};

} // namespace daw::model
