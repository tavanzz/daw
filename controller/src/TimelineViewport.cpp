#include "daw/controller/TimelineViewport.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace daw::controller
{

namespace
{

// Round steps for the time ruler, in milliseconds.
constexpr std::array<juce::int64, 22> timeStepsMs{1,     2,     5,      10,     20,     50,
                                                  100,   200,   500,    1000,   2000,   5000,
                                                  10000, 15000, 30000,  60000,  120000, 300000,
                                                  600000, 1200000, 1800000, 3600000};

juce::String formatAtStep(model::Ticks position, juce::int64 stepMs)
{
    const auto totalMs = static_cast<juce::int64>(
        std::llround(model::ticksToSeconds(position) * 1000.0));
    const auto hours = totalMs / 3600000;
    const auto minutes = (totalMs / 60000) % 60;
    const auto seconds = (totalMs / 1000) % 60;
    const auto milliseconds = totalMs % 1000;

    juce::String text;
    if (hours > 0)
        text << hours << ":" << juce::String(minutes).paddedLeft('0', 2);
    else
        text << minutes;
    text << ":" << juce::String(seconds).paddedLeft('0', 2);

    if (stepMs < 10)
        text << "." << juce::String(milliseconds).paddedLeft('0', 3);
    else if (stepMs < 100)
        text << "." << juce::String(milliseconds / 10).paddedLeft('0', 2);
    else if (stepMs < 1000)
        text << "." << juce::String(milliseconds / 100);

    return text;
}

} // namespace

void TimelineViewport::setWidth(int widthInPixels) noexcept
{
    width = std::max(1, widthInPixels);
}

void TimelineViewport::setTicksPerPixel(double newTicksPerPixel) noexcept
{
    if (std::isfinite(newTicksPerPixel))
        ticksPerPixel = std::clamp(newTicksPerPixel, minimumTicksPerPixel, maximumTicksPerPixel);
}

void TimelineViewport::setStart(model::Ticks newStart) noexcept
{
    start = std::max(model::Ticks{0}, newStart);
}

model::Ticks TimelineViewport::getEnd() const noexcept
{
    return xToTicks(width);
}

double TimelineViewport::ticksToX(model::Ticks position) const noexcept
{
    return static_cast<double>(position - start) / ticksPerPixel;
}

model::Ticks TimelineViewport::xToTicks(double x) const noexcept
{
    return start + static_cast<model::Ticks>(std::llround(x * ticksPerPixel));
}

void TimelineViewport::zoomAround(double factor, double anchorX) noexcept
{
    if (!(factor > 0.0))
        return;

    const model::Ticks anchor = xToTicks(anchorX);
    setTicksPerPixel(ticksPerPixel / factor);
    setStart(anchor - static_cast<model::Ticks>(std::llround(anchorX * ticksPerPixel)));
}

void TimelineViewport::scrollByPixels(double pixels) noexcept
{
    setStart(start + static_cast<model::Ticks>(std::llround(pixels * ticksPerPixel)));
}

void TimelineViewport::showRange(model::Ticks from, model::Ticks to) noexcept
{
    if (to <= from)
        return;

    setTicksPerPixel(static_cast<double>(to - from) / width);
    setStart(from);
}

bool TimelineViewport::keepInView(model::Ticks position) noexcept
{
    if (position >= start && position < getEnd())
        return false;

    // Page so the position lands near the left edge, as most DAWs do when following.
    const auto margin = static_cast<model::Ticks>(std::llround(0.05 * width * ticksPerPixel));
    setStart(position - margin);
    return true;
}

model::GridDivision TimelineViewport::resolveGrid(model::GridDivision setting,
                                                  const model::TempoMap& tempoMap) const noexcept
{
    if (setting != model::GridDivision::automatic)
        return setting;

    return model::Grid::automaticDivision(tempoMap, ticksPerPixel, minimumGridSpacingPixels);
}

std::vector<RulerMark> timeRulerMarks(const TimelineViewport& viewport,
                                      double minimumLabelSpacing)
{
    const double ticksPerMs = static_cast<double>(model::ticksPerSecond) / 1000.0;
    const double pixelsPerMs = ticksPerMs / viewport.getTicksPerPixel();

    juce::int64 majorMs = timeStepsMs.back();
    for (const auto step : timeStepsMs)
    {
        if (static_cast<double>(step) * pixelsPerMs >= minimumLabelSpacing)
        {
            majorMs = step;
            break;
        }
    }

    // Minor marks: the finest round step that divides the major one and is at least
    // 8 pixels wide.
    juce::int64 minorMs = majorMs;
    for (const auto step : timeStepsMs)
    {
        if (step >= majorMs)
            break;
        if (majorMs % step == 0 && static_cast<double>(step) * pixelsPerMs >= 8.0)
        {
            minorMs = step;
            break;
        }
    }

    const auto minorTicks = minorMs * (model::ticksPerSecond / 1000);
    const auto first = viewport.getStart() / minorTicks;
    const auto last = viewport.getEnd() / minorTicks + 1;

    std::vector<RulerMark> marks;
    for (auto index = first; index <= last; ++index)
    {
        const model::Ticks position = index * minorTicks;
        const bool major = (index * minorMs) % majorMs == 0;
        const auto label = major ? formatAtStep(position, majorMs) : juce::String();
        marks.push_back({position, major, label});
    }

    return marks;
}

std::vector<RulerMark> barRulerMarks(const TimelineViewport& viewport,
                                     const model::TempoMap& tempoMap, double minimumLabelSpacing)
{
    const double barPixels = tempoMap.ticksPerBar() / viewport.getTicksPerPixel();
    const double beatPixels = tempoMap.ticksPerBeat() / viewport.getTicksPerPixel();

    juce::int64 labelEvery = 1;
    while (static_cast<double>(labelEvery) * barPixels < minimumLabelSpacing &&
           labelEvery < (juce::int64{1} << 20))
        labelEvery *= 2;

    const bool showBars = barPixels >= 4.0;
    const bool showBeats = beatPixels >= 8.0;
    const bool labelBeats = beatPixels >= minimumLabelSpacing;
    const int beatsPerBar = tempoMap.getTimeSignature().numerator;

    std::vector<RulerMark> marks;
    const auto firstBar = tempoMap.barAt(viewport.getStart());
    const auto lastBar = tempoMap.barAt(viewport.getEnd());

    for (auto bar = firstBar; bar <= lastBar; ++bar)
    {
        const bool labelled = bar % labelEvery == 0;
        if (labelled || showBars)
            marks.push_back({tempoMap.barStart(bar), labelled,
                             labelled ? juce::String(bar + 1) : juce::String()});

        if (!showBeats)
            continue;

        for (int beat = 1; beat < beatsPerBar; ++beat)
            marks.push_back({tempoMap.beatStart(bar, beat), false,
                             labelBeats ? juce::String(bar + 1) + "." + juce::String(beat + 1)
                                        : juce::String()});
    }

    return marks;
}

juce::String formatTime(model::Ticks position)
{
    return formatAtStep(std::max(model::Ticks{0}, position), 1);
}

} // namespace daw::controller
