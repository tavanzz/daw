#include "daw/engine/dsp/Metronome.h"

#include "support/RealtimeCheck.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <vector>

using daw::engine::dsp::Metronome;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr float gain = 0.5f;

// Beat b at 1000 b + 7, four to a bar.
class EvenGrid final : public daw::engine::BeatGrid
{
public:
    juce::int64 beatPosition(juce::int64 beat) const noexcept override { return beat * 1000 + 7; }
    int beatsPerBar() const noexcept override { return 4; }
    double approximateBeatLength() const noexcept override { return 1003.0; }
};

// What the metronome should play over [start, start + length) when it starts at start:
// each beat's click from its sample on, cut by the next one.
std::vector<float> expectedClicks(const Metronome& metronome, const daw::engine::BeatGrid& grid,
                                  juce::int64 start, int length)
{
    std::vector<float> expected(static_cast<std::size_t>(length), 0.0f);
    for (auto beat = grid.firstBeatFrom(start); grid.beatPosition(beat) < start + length;
         ++beat)
    {
        const auto& click = metronome.getClick(grid.isDownbeat(beat));
        for (std::size_t j = 0; j < click.size(); ++j)
        {
            const auto index = grid.beatPosition(beat) + static_cast<juce::int64>(j) - start;
            if (index >= 0 && index < length)
                expected[static_cast<std::size_t>(index)] = click[j] * gain;
        }
    }
    return expected;
}

} // namespace

TEST_CASE("Beat grids find the first beat at or after a position", "[engine][metronome]")
{
    const EvenGrid grid;
    CHECK(grid.firstBeatFrom(7) == 0);
    CHECK(grid.firstBeatFrom(8) == 1);
    CHECK(grid.firstBeatFrom(1007) == 1);
    CHECK(grid.firstBeatFrom(-993) == -1);
    CHECK(grid.firstBeatFrom(-994) == -1);
    CHECK(grid.firstBeatFrom(-1000000) == -1000);
    CHECK(grid.isDownbeat(0));
    CHECK(grid.isDownbeat(-4));
    CHECK_FALSE(grid.isDownbeat(-1));
    CHECK_FALSE(grid.isDownbeat(5));
}

TEST_CASE("Clicks start on the exact sample of every beat, however blocks are cut",
          "[engine][metronome]")
{
    const EvenGrid grid;
    Metronome metronome;
    metronome.prepare(sampleRate);

    const auto& accent = metronome.getClick(true);
    const auto& normal = metronome.getClick(false);
    REQUIRE(accent.size() == 1440);
    CHECK(accent.front() == 1.0f);        // the first sample marks the beat
    CHECK(normal.front() == 0.6f);
    CHECK(accent.back() == 0.0f);         // and the click ends in silence
    CHECK(accent.size() > 1000);          // longer than a beat here: the next one cuts it

    const juce::int64 start = -2500;
    const int length = 12000;
    std::vector<float> left(static_cast<std::size_t>(length), 0.0f);
    std::vector<float> right(static_cast<std::size_t>(length), 0.0f);

    std::size_t heapOperations = 0;
    const int blockSizes[] = {1, 7, 256, 1000, 3};
    for (int done = 0, block = 0; done < length; ++block)
    {
        const int size = std::min(blockSizes[block % 5], length - done);
        const auto offset = static_cast<std::size_t>(done);
        heapOperations += daw::test::heapOperationsDuring([&] {
            metronome.render(&grid, start + done, gain, left.data() + offset,
                             right.data() + offset, size);
        });
        done += size;
    }

    CHECK(heapOperations == 0);
    CHECK(left == expectedClicks(metronome, grid, start, length));
    CHECK(right == left);
}

TEST_CASE("A ringing click finishes after a jump or when no new clicks are wanted",
          "[engine][metronome]")
{
    const EvenGrid grid;
    Metronome metronome;
    metronome.prepare(sampleRate);
    const auto& accent = metronome.getClick(true);

    std::vector<float> left(3000, 0.0f);
    std::vector<float> right(3000, 0.0f);

    // A downbeat at 7, then the timeline jumps to 100000 (no beat there for 7
    // samples), then rendering continues without new clicks.
    metronome.render(&grid, 0, 1.0f, left.data(), right.data(), 100);
    metronome.render(&grid, 99993, 1.0f, left.data() + 100, right.data() + 100, 6);
    metronome.render(nullptr, 0, 1.0f, left.data() + 106, right.data() + 106, 2894);

    bool continuous = true;
    for (std::size_t j = 0; j < accent.size(); ++j)
        continuous = continuous && left[j + 7] == accent[j];
    CHECK(continuous);
    CHECK(left[6] == 0.0f);
    CHECK(left[7 + accent.size()] == 0.0f);

    metronome.render(&grid, 0, 1.0f, left.data(), right.data(), 20);
    metronome.stop();
    std::fill(left.begin(), left.end(), 0.0f);
    metronome.render(nullptr, 0, 1.0f, left.data(), right.data(), 100);
    CHECK(std::all_of(left.begin(), left.end(), [](float x) { return x == 0.0f; }));
}
