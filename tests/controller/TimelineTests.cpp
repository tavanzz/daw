#include "daw/controller/ClipEditing.h"
#include "daw/controller/TimelineViewport.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using daw::controller::minimumTrimmedLength;
using daw::controller::RulerMark;
using daw::controller::Snapper;
using daw::controller::TimelineViewport;
using daw::model::ClipBounds;
using daw::model::Grid;
using daw::model::GridDivision;
using daw::model::TempoMap;
using daw::model::Ticks;
using daw::model::ticksPerSecond;

namespace
{

TimelineViewport makeViewport(double secondsPerPixel, int width = 1000, Ticks start = 0)
{
    TimelineViewport viewport;
    viewport.setWidth(width);
    viewport.setTicksPerPixel(secondsPerPixel * static_cast<double>(ticksPerSecond));
    viewport.setStart(start);
    return viewport;
}

std::vector<RulerMark> labelled(const std::vector<RulerMark>& marks)
{
    std::vector<RulerMark> result;
    std::copy_if(marks.begin(), marks.end(), std::back_inserter(result),
                 [](const RulerMark& mark) { return mark.label.isNotEmpty(); });
    return result;
}

} // namespace

TEST_CASE("The viewport converts between pixels and ticks", "[controller][viewport]")
{
    const auto viewport = makeViewport(0.01, 800, ticksPerSecond * 5);

    CHECK(viewport.xToTicks(0) == ticksPerSecond * 5);
    CHECK(viewport.xToTicks(100) == ticksPerSecond * 6);
    CHECK(viewport.ticksToX(ticksPerSecond * 6) == 100.0);
    CHECK(viewport.getEnd() == ticksPerSecond * 13);
}

TEST_CASE("Zooming keeps the point under the pointer in place", "[controller][viewport]")
{
    auto viewport = makeViewport(0.01, 1000, ticksPerSecond * 10);
    const Ticks underPointer = viewport.xToTicks(300);

    viewport.zoomAround(4.0, 300);
    CHECK(viewport.getTicksPerPixel() == 0.0025 * static_cast<double>(ticksPerSecond));
    CHECK(std::abs(viewport.xToTicks(300) - underPointer) <= 1);

    viewport.zoomAround(0.25, 300);
    CHECK(std::abs(viewport.xToTicks(300) - underPointer) <= 1);

    // Zooming out near the start never scrolls before 0.
    auto nearStart = makeViewport(0.01, 1000, 0);
    nearStart.zoomAround(0.1, 900);
    CHECK(nearStart.getStart() == 0);
}

TEST_CASE("Zoom and scroll stay within limits", "[controller][viewport]")
{
    auto viewport = makeViewport(1.0);
    viewport.setTicksPerPixel(1.0);
    CHECK(viewport.getTicksPerPixel() == TimelineViewport::minimumTicksPerPixel);
    viewport.setTicksPerPixel(1e15);
    CHECK(viewport.getTicksPerPixel() == TimelineViewport::maximumTicksPerPixel);

    viewport.setStart(ticksPerSecond);
    viewport.scrollByPixels(-1e9);
    CHECK(viewport.getStart() == 0);
}

TEST_CASE("Show range and keep in view", "[controller][viewport]")
{
    auto viewport = makeViewport(0.01, 1000);
    viewport.showRange(ticksPerSecond * 20, ticksPerSecond * 40);
    CHECK(viewport.getStart() == ticksPerSecond * 20);
    CHECK(viewport.getEnd() == ticksPerSecond * 40);

    CHECK_FALSE(viewport.keepInView(ticksPerSecond * 30));
    CHECK(viewport.keepInView(ticksPerSecond * 41));
    CHECK(viewport.getStart() < ticksPerSecond * 41);
    CHECK(viewport.getEnd() > ticksPerSecond * 41);
}

TEST_CASE("The time ruler labels round values far enough apart", "[controller][ruler]")
{
    SECTION("10 ms per pixel: a label every second")
    {
        const auto marks = timeRulerMarks(makeViewport(0.01, 1000), 70.0);
        const auto labels = labelled(marks);
        REQUIRE(labels.size() >= 10);
        CHECK(labels[0].label == "0:00");
        CHECK(labels[1].position == ticksPerSecond);
        CHECK(labels[1].label == "0:01");
        // Minor marks in between.
        CHECK(marks.size() > labels.size());
    }

    SECTION("1 ms per pixel: tenths of a second")
    {
        const auto viewport = makeViewport(0.001, 1000, ticksPerSecond * 61);
        const auto labels = labelled(timeRulerMarks(viewport, 70.0));
        REQUIRE(labels.size() >= 2);
        CHECK(labels[0].label == "1:01.0");
        CHECK(labels[1].label == "1:01.1");
    }

    SECTION("1 s per pixel: minutes")
    {
        const auto labels = labelled(timeRulerMarks(makeViewport(1.0, 1000), 70.0));
        REQUIRE(labels.size() >= 2);
        CHECK(labels[1].position - labels[0].position == 120 * ticksPerSecond);
        CHECK(labels[1].label == "2:00");
    }

    SECTION("Hours")
    {
        CHECK(daw::controller::formatTime(ticksPerSecond * 3725 + ticksPerSecond / 4) ==
              "1:02:05.250");
        CHECK(daw::controller::formatTime(0) == "0:00.000");
    }

    SECTION("Labels never closer than asked")
    {
        for (const double secondsPerPixel : {0.0001, 0.003, 0.05, 0.4, 7.0})
        {
            const auto viewport = makeViewport(secondsPerPixel, 1200);
            const auto labels = labelled(timeRulerMarks(viewport, 70.0));
            for (std::size_t i = 1; i < labels.size(); ++i)
                REQUIRE(viewport.ticksToX(labels[i].position) -
                            viewport.ticksToX(labels[i - 1].position) >=
                        70.0 - 1e-6);
        }
    }
}

TEST_CASE("The bar ruler numbers bars as space allows", "[controller][ruler]")
{
    const TempoMap map(120.0, {4, 4}); // a bar is 2 s

    SECTION("Wide bars: every bar labelled, beats labelled too")
    {
        const auto viewport = makeViewport(0.005, 1000); // a beat is 100 px
        const auto labels = labelled(barRulerMarks(viewport, map, 40.0));
        REQUIRE(labels.size() >= 5);
        CHECK(labels[0].label == "1");
        CHECK(labels[1].label == "1.2");
        CHECK(labels[4].label == "2");
        CHECK(labels[4].major);
    }

    SECTION("Narrow bars: every few bars")
    {
        const auto viewport = makeViewport(0.2, 1000); // a bar is 10 px
        const auto labels = labelled(barRulerMarks(viewport, map, 40.0));
        REQUIRE(labels.size() >= 2);
        CHECK(labels[0].label == "1");
        CHECK(labels[1].label == "5"); // every 4 bars: 40 px apart
    }
}

TEST_CASE("The automatic grid depends on the zoom", "[controller][viewport]")
{
    const TempoMap map(120.0, {4, 4});
    const auto close = makeViewport(0.01); // a sixteenth is 12.5 px
    const auto far = makeViewport(0.1); // a bar is 20 px, a half note 10

    CHECK(close.resolveGrid(GridDivision::automatic, map) == GridDivision::sixteenth);
    CHECK(far.resolveGrid(GridDivision::automatic, map) == GridDivision::bar);
    CHECK(far.resolveGrid(GridDivision::eighth, map) == GridDivision::eighth);
}

TEST_CASE("Moving a clip snaps its start and stops at 0", "[controller][editing]")
{
    const Snapper beats(Grid(TempoMap(120.0, {4, 4}), GridDivision::quarter));
    const Ticks beat = ticksPerSecond / 2;

    CHECK(daw::controller::movedPosition(beat, beat + beat / 3, beats) == 2 * beat);
    CHECK(daw::controller::movedPosition(beat, -5 * beat, beats) == 0);
    CHECK(daw::controller::movedPosition(beat, 1234, Snapper()) == beat + 1234);
}

TEST_CASE("Trimming the start keeps the end and the audio in place", "[controller][editing]")
{
    const Snapper none;
    const ClipBounds clip{1000000, 300000, 2000000}; // starts 300000 ticks into its file

    // Shorter at the start: the offset grows by as much as the start moves.
    CHECK(daw::controller::trimmedStart(clip, 1500000, none) ==
          ClipBounds{1500000, 800000, 1500000});

    // Longer: back to the start of the file, not beyond.
    CHECK(daw::controller::trimmedStart(clip, 0, none) == ClipBounds{700000, 0, 2300000});

    // Never shorter than the minimum.
    CHECK(daw::controller::trimmedStart(clip, 9000000, none) ==
          ClipBounds{3000000 - minimumTrimmedLength, 300000 + 2000000 - minimumTrimmedLength,
                     minimumTrimmedLength});

    // Never before 0, even when the file would allow it.
    const ClipBounds early{100, 5000000, 1000000};
    CHECK(daw::controller::trimmedStart(early, -999999, none).position == 0);

    // Unknown length: nothing to trim.
    const ClipBounds unknown{0, 0, std::nullopt};
    CHECK(daw::controller::trimmedStart(unknown, 500, none) == unknown);
}

TEST_CASE("Trimming the end stops at the end of the file", "[controller][editing]")
{
    const Snapper none;
    const ClipBounds clip{1000000, 300000, 2000000};
    const Ticks fileLength = 4000000;

    CHECK(daw::controller::trimmedEnd(clip, 2000000, fileLength, none) ==
          ClipBounds{1000000, 300000, 1000000});
    CHECK(daw::controller::trimmedEnd(clip, 99999999, fileLength, none) ==
          ClipBounds{1000000, 300000, 3700000});
    CHECK(daw::controller::trimmedEnd(clip, 99999999, std::nullopt, none).length == 98999999);
    CHECK(daw::controller::trimmedEnd(clip, 0, fileLength, none).length == minimumTrimmedLength);
}

TEST_CASE("Trimmed edges snap to the grid", "[controller][editing]")
{
    const Snapper beats(Grid(TempoMap(120.0, {4, 4}), GridDivision::quarter));
    const Ticks beat = ticksPerSecond / 2;
    const ClipBounds clip{beat, 0, 4 * beat};

    CHECK(daw::controller::trimmedStart(clip, 2 * beat + beat / 4, beats) ==
          ClipBounds{2 * beat, beat, 3 * beat});
    CHECK(daw::controller::trimmedEnd(clip, 3 * beat + beat / 3, 100 * beat, beats) ==
          ClipBounds{beat, 0, 2 * beat});
}
