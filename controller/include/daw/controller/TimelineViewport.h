#pragma once

#include "daw/model/Grid.h"
#include "daw/model/TempoMap.h"

#include <juce_core/juce_core.h>

#include <vector>

namespace daw::controller
{

/**
    Which part of the timeline a view shows, and how big: the zoom (ticks per
    pixel), the scroll position (the tick at x = 0) and the width in pixels.
    Conversions between pixels and ticks, zooming and scrolling live here so that
    every UI (mouse or touch) behaves the same. Plain value type.
*/
class TimelineViewport
{
public:
    /** From about 4 pixels per sample at 48 kHz to 10 seconds per pixel. */
    static constexpr double minimumTicksPerPixel = 1470.0;
    static constexpr double maximumTicksPerPixel =
        10.0 * static_cast<double>(model::ticksPerSecond);

    /** Lines of the automatic grid are at least this far apart. */
    static constexpr double minimumGridSpacingPixels = 12.0;

    TimelineViewport() = default;

    void setWidth(int widthInPixels) noexcept;
    [[nodiscard]] int getWidth() const noexcept { return width; }

    [[nodiscard]] double getTicksPerPixel() const noexcept { return ticksPerPixel; }
    void setTicksPerPixel(double newTicksPerPixel) noexcept; ///< clamped to the limits

    /** The tick at x = 0 (never negative). */
    [[nodiscard]] model::Ticks getStart() const noexcept { return start; }
    void setStart(model::Ticks newStart) noexcept;

    /** The tick at x = width. */
    [[nodiscard]] model::Ticks getEnd() const noexcept;

    [[nodiscard]] double ticksToX(model::Ticks position) const noexcept;
    [[nodiscard]] model::Ticks xToTicks(double x) const noexcept;

    /** Zooms by factor (above 1 zooms in) keeping the tick under anchorX in place. */
    void zoomAround(double factor, double anchorX) noexcept;

    void scrollByPixels(double pixels) noexcept;

    /** Shows [from, to) across the whole width. */
    void showRange(model::Ticks from, model::Ticks to) noexcept;

    /** Scrolls, by whole pages, so that position is in view (for following the
        playhead). Returns true if it scrolled. */
    bool keepInView(model::Ticks position) noexcept;

    /** The grid to draw and snap to at this zoom: setting, or for automatic the
        finest division with lines at least minimumGridSpacingPixels apart. */
    [[nodiscard]] model::GridDivision resolveGrid(model::GridDivision setting,
                                                  const model::TempoMap& tempoMap) const noexcept;

private:
    int width = 1000;
    double ticksPerPixel = static_cast<double>(model::ticksPerSecond) / 100.0; // 10 ms
    model::Ticks start = 0;
};

/** A tick on a ruler; major ones carry a label. */
struct RulerMark
{
    model::Ticks position = 0;
    bool major = false;
    juce::String label;

    bool operator==(const RulerMark&) const = default;
};

/** Marks for a ruler in minutes and seconds: labels at least minimumLabelSpacing
    pixels apart, on round values (1, 2, 5, 10... ms or s, then minutes). */
[[nodiscard]] std::vector<RulerMark> timeRulerMarks(const TimelineViewport& viewport,
                                                    double minimumLabelSpacing = 70.0);

/** Marks for a ruler in bars and beats: bar numbers (every 1, 2, 4, 8... bars as
    space allows), beats as minor marks, labelled "bar.beat" when there is room. */
[[nodiscard]] std::vector<RulerMark> barRulerMarks(const TimelineViewport& viewport,
                                                   const model::TempoMap& tempoMap,
                                                   double minimumLabelSpacing = 40.0);

/** "m:ss.mmm" (or "h:mm:ss.mmm" from one hour). */
[[nodiscard]] juce::String formatTime(model::Ticks position);

} // namespace daw::controller
