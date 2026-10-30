#include "daw/model/Grid.h"
#include "daw/model/TempoMap.h"
#include "daw/model/TimelineTime.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <array>
#include <limits>
#include <random>

using Catch::Matchers::WithinAbs;
using daw::model::Grid;
using daw::model::GridDivision;
using daw::model::samplesToTicks;
using daw::model::TempoMap;
using daw::model::Ticks;
using daw::model::ticksPerSecond;
using daw::model::ticksToSamples;
using daw::model::TimeSignature;

namespace
{

constexpr std::array commonSampleRates{8000.0,   11025.0,  16000.0,  22050.0,  24000.0,
                                       32000.0,  44100.0,  48000.0,  88200.0,  96000.0,
                                       176400.0, 192000.0, 352800.0, 384000.0};

} // namespace

TEST_CASE("Every common sample rate divides the tick rate", "[model][timeline]")
{
    for (const double rate : commonSampleRates)
        CHECK(ticksPerSecond % static_cast<Ticks>(rate) == 0);
}

TEST_CASE("Sample positions survive a round trip through ticks at every common rate",
          "[model][timeline]")
{
    std::mt19937_64 random(42);
    // Up to about 10 hours at the highest rate.
    std::uniform_int_distribution<juce::int64> anySample(0, juce::int64{384000} * 36000);

    for (const double rate : commonSampleRates)
    {
        for (int i = 0; i < 1000; ++i)
        {
            const auto sample = anySample(random);
            REQUIRE(ticksToSamples(samplesToTicks(sample, rate), rate) == sample);
        }

        // One sample is exactly ticksPerSecond / rate ticks.
        CHECK(samplesToTicks(1, rate) * static_cast<Ticks>(rate) == ticksPerSecond);
    }
}

TEST_CASE("Ticks between two samples go to the nearest one", "[model][timeline]")
{
    // At 48 kHz one sample is 5880 ticks.
    CHECK(ticksToSamples(0, 48000.0) == 0);
    CHECK(ticksToSamples(2939, 48000.0) == 0);
    CHECK(ticksToSamples(2940, 48000.0) == 1); // halfway rounds up
    CHECK(ticksToSamples(5880, 48000.0) == 1);
    CHECK(ticksToSamples(-2941, 48000.0) == -1);

    // A rate that does not divide the tick rate still rounds to the nearest sample.
    const double oddRate = 44056.0;
    CHECK(ticksToSamples(ticksPerSecond, oddRate) == 44056);
    CHECK(ticksToSamples(samplesToTicks(12345, oddRate), oddRate) == 12345);
}

TEST_CASE("Seconds convert to ticks and back", "[model][timeline]")
{
    CHECK(daw::model::secondsToTicks(1.0) == ticksPerSecond);
    CHECK(daw::model::secondsToTicks(0.5) == ticksPerSecond / 2);
    CHECK_THAT(daw::model::ticksToSeconds(ticksPerSecond * 90), WithinAbs(90.0, 0.0));
}

TEST_CASE("Time signatures accept 1 to 32 beats of power-of-two notes", "[model][timeline]")
{
    CHECK(TimeSignature{4, 4}.isValid());
    CHECK(TimeSignature{7, 8}.isValid());
    CHECK(TimeSignature{1, 1}.isValid());
    CHECK(TimeSignature{32, 32}.isValid());
    CHECK_FALSE(TimeSignature{0, 4}.isValid());
    CHECK_FALSE(TimeSignature{33, 4}.isValid());
    CHECK_FALSE(TimeSignature{4, 3}.isValid());
    CHECK_FALSE(TimeSignature{4, 64}.isValid());
    CHECK_FALSE(TimeSignature{4, 0}.isValid());
}

TEST_CASE("A tempo map clamps the tempo and rejects invalid metres", "[model][timeline]")
{
    CHECK(TempoMap(5.0, {4, 4}).getTempo() == TempoMap::minimumTempo);
    CHECK(TempoMap(5000.0, {4, 4}).getTempo() == TempoMap::maximumTempo);
    CHECK(TempoMap(std::numeric_limits<double>::quiet_NaN(), {4, 4}).getTempo() ==
          TempoMap::defaultTempo);
    CHECK(TempoMap(100.0, {5, 3}).getTimeSignature() == TimeSignature{4, 4});
}

TEST_CASE("Bar and beat lengths follow the tempo in quarter notes", "[model][timeline]")
{
    SECTION("4/4 at 120: bars of 2 s, beats of 0.5 s")
    {
        const TempoMap map(120.0, {4, 4});
        CHECK(map.barStart(1) == 2 * ticksPerSecond);
        CHECK(map.beatStart(0, 1) == ticksPerSecond / 2);
        CHECK(map.beatStart(3, 2) == 7 * ticksPerSecond);
    }

    SECTION("6/8 at 120: bars of 1.5 s, eighth-note beats of 0.25 s")
    {
        const TempoMap map(120.0, {6, 8});
        CHECK(map.barStart(2) == 3 * ticksPerSecond);
        CHECK(map.beatStart(0, 1) == ticksPerSecond / 4);
    }

    SECTION("7/8 at 90: bars of 3.5 quarter notes")
    {
        const TempoMap map(90.0, {7, 8});
        // One quarter note lasts 2/3 s.
        CHECK(map.barStart(3) == ticksPerSecond * 7); // 3 bars = 10.5 quarters = 7 s
    }
}

TEST_CASE("A position on a bar or beat reads back as that bar and beat at any tempo",
          "[model][timeline]")
{
    const double tempo = GENERATE(120.0, 133.0, 97.31, 20.0, 999.0);
    const TimeSignature signature = GENERATE(TimeSignature{4, 4}, TimeSignature{7, 8},
                                             TimeSignature{3, 4}, TimeSignature{5, 16});
    const TempoMap map(tempo, signature);

    for (juce::int64 bar = 0; bar < 2000; bar += 7)
    {
        for (int beat = 0; beat < signature.numerator; ++beat)
        {
            const Ticks start = map.beatStart(bar, beat);
            const auto musical = map.toMusical(start);
            REQUIRE(musical.bar == bar);
            REQUIRE(musical.beat == beat);
            REQUIRE(musical.beatFraction == 0.0);

            // One tick earlier is still in the previous beat.
            const auto before = map.toMusical(start - 1);
            REQUIRE((before.bar < bar || before.beat < beat));
            REQUIRE(before.beatFraction > 0.99);
        }

        REQUIRE(map.barAt(map.barStart(bar)) == bar);
        REQUIRE(map.barAt(map.barStart(bar + 1) - 1) == bar);
    }
}

TEST_CASE("Positions are shown as bar.beat.sixteenth counting from 1", "[model][timeline]")
{
    const TempoMap fourFour(120.0, {4, 4});
    CHECK(fourFour.formatBarsBeats(0) == "1.1.1");
    CHECK(fourFour.formatBarsBeats(ticksPerSecond / 8) == "1.1.2");  // one sixteenth
    CHECK(fourFour.formatBarsBeats(ticksPerSecond * 2) == "2.1.1");  // bar 2
    CHECK(fourFour.formatBarsBeats(ticksPerSecond * 2 - 1) == "1.4.4"); // just before it

    const TempoMap sixEight(120.0, {6, 8});
    CHECK(sixEight.formatBarsBeats(ticksPerSecond / 4 * 5) == "1.6.1");
    CHECK(sixEight.formatBarsBeats(ticksPerSecond / 8 * 11) == "1.6.2");
}

TEST_CASE("Grid lines restart at every bar line", "[model][timeline][grid]")
{
    // 7/8 at 120: a quarter note is 0.5 s, a bar is 1.75 s.
    const TempoMap map(120.0, {7, 8});
    const Grid quarters(map, GridDivision::quarter);
    const Ticks quarter = ticksPerSecond / 2;
    const Ticks bar = ticksPerSecond * 7 / 4;

    const std::vector<Ticks> expected{0,
                                      quarter,
                                      2 * quarter,
                                      3 * quarter,
                                      bar,
                                      bar + quarter,
                                      bar + 2 * quarter,
                                      bar + 3 * quarter,
                                      2 * bar};
    CHECK(quarters.linesBetween(0, 2 * bar + 1) == expected);

    // The last quarter of a 7/8 bar is an eighth long: 3.25 quarters snaps back,
    // 3.3 quarters snaps forward to the bar line.
    CHECK(quarters.snap(3 * quarter + quarter / 4) == 3 * quarter);
    CHECK(quarters.snap(3 * quarter + quarter / 4 + quarter / 20) == bar);
}

TEST_CASE("Snapping goes to the nearest line, the earlier one when halfway",
          "[model][timeline][grid]")
{
    const TempoMap map(120.0, {4, 4});
    const Grid beats(map, GridDivision::quarter);
    const Ticks beat = ticksPerSecond / 2;

    CHECK(beats.snap(0) == 0);
    CHECK(beats.snap(beat) == beat);
    CHECK(beats.snap(beat + beat / 2 - 1) == beat);
    CHECK(beats.snap(beat + beat / 2) == beat); // halfway
    CHECK(beats.snap(beat + beat / 2 + 1) == 2 * beat);
    CHECK(beats.lineAtOrBefore(beat * 5 - 1) == beat * 4);
    CHECK(beats.lineAfter(beat * 4) == beat * 5);

    const Grid bars(map, GridDivision::bar);
    CHECK(bars.snap(ticksPerSecond * 3) == ticksPerSecond * 2); // halfway between bars 2 and 3
    CHECK(bars.snap(ticksPerSecond * 3 + 1) == ticksPerSecond * 4);
}

TEST_CASE("Every grid line snaps to itself at awkward tempos", "[model][timeline][grid]")
{
    const double tempo = GENERATE(133.0, 97.31, 61.7);
    const auto division = GENERATE(GridDivision::bar, GridDivision::quarter,
                                   GridDivision::sixteenth, GridDivision::thirtySecond);
    const TempoMap map(tempo, {5, 4});
    const Grid grid(map, division);

    const auto lines = grid.linesBetween(0, map.barStart(40));
    REQUIRE(lines.size() >= 40);

    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        REQUIRE(grid.snap(lines[i]) == lines[i]);
        REQUIRE(grid.lineAtOrBefore(lines[i]) == lines[i]);
        if (i + 1 < lines.size())
        {
            REQUIRE(lines[i] < lines[i + 1]);
            REQUIRE(grid.lineAfter(lines[i]) == lines[i + 1]);
        }
    }
}

TEST_CASE("Grids coarser than a bar fall back to bar lines", "[model][timeline][grid]")
{
    // A 2/8 bar is shorter than a half note.
    const TempoMap map(120.0, {2, 8});
    const Grid halves(map, GridDivision::half);
    const Ticks bar = ticksPerSecond / 2;

    CHECK(halves.linesBetween(0, 3 * bar) == std::vector<Ticks>{0, bar, 2 * bar});
}

TEST_CASE("The automatic grid is the finest one that is not too dense", "[model][timeline][grid]")
{
    const TempoMap map(120.0, {4, 4});
    const double quarter = static_cast<double>(ticksPerSecond) / 2.0;

    // At least 16 pixels between lines; ticksPerPixel = quarter / n puts quarters n
    // pixels apart.
    CHECK(Grid::automaticDivision(map, quarter / 1000.0, 16.0) == GridDivision::thirtySecond);
    CHECK(Grid::automaticDivision(map, quarter / 128.0, 16.0) == GridDivision::thirtySecond);
    CHECK(Grid::automaticDivision(map, quarter / 127.0, 16.0) == GridDivision::sixteenth);
    CHECK(Grid::automaticDivision(map, quarter / 16.0, 16.0) == GridDivision::quarter);
    CHECK(Grid::automaticDivision(map, quarter / 15.0, 16.0) == GridDivision::half);
    CHECK(Grid::automaticDivision(map, quarter, 16.0) == GridDivision::bar); // even bars too dense
}
