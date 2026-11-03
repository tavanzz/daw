#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/TempoBeatGrid.h"
#include "daw/engine/Engine.h"
#include "daw/engine/OfflineRenderer.h"
#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <limits>
#include <vector>

using daw::engine::Engine;
using daw::engine::dsp::Metronome;
using daw::model::Project;
using daw::model::TempoMap;
using daw::model::ticksToSamples;

namespace
{

constexpr float metronomeGain = 0.5f;

std::unique_ptr<daw::engine::Arrangement> clickingArrangement(const Project& project, double rate,
                                                              juce::int64 length)
{
    auto arrangement = daw::controller::buildArrangement(project, rate, {});
    arrangement->length = length;
    return arrangement;
}

// The clicks the metronome should play when it starts at timeline position start: one
// on every beat of the tempo map from start on and before beatsBefore, placed
// independently of the engine's grid.
std::vector<float> expectedClicks(const TempoMap& tempoMap, double rate, juce::int64 start,
                                  int length,
                                  juce::int64 beatsBefore = std::numeric_limits<juce::int64>::max())
{
    Metronome metronome;
    metronome.prepare(rate);
    std::vector<float> expected(static_cast<std::size_t>(length), 0.0f);
    const int beatsPerBar = tempoMap.getTimeSignature().numerator;

    for (juce::int64 bar = -3;; ++bar)
    {
        const auto barPosition = ticksToSamples(tempoMap.barStart(bar), rate);
        if (barPosition >= start + length)
            break;

        for (int beat = 0; beat < beatsPerBar; ++beat)
        {
            const auto position = ticksToSamples(tempoMap.beatStart(bar, beat), rate);
            if (position < start || position >= beatsBefore)
                continue;

            const auto& click = metronome.getClick(beat == 0);
            for (std::size_t j = 0; j < click.size(); ++j)
            {
                const auto index = position + static_cast<juce::int64>(j) - start;
                if (index >= 0 && index < length)
                    expected[static_cast<std::size_t>(index)] = click[j] * metronomeGain;
            }
        }
    }
    return expected;
}

std::vector<float> play(Engine& engine, int length, int blockSize, std::size_t& heapOperations)
{
    std::vector<float> played;
    juce::AudioBuffer<float> block(2, blockSize);
    while (static_cast<int>(played.size()) < length)
    {
        heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
        played.insert(played.end(), block.getReadPointer(0),
                      block.getReadPointer(0) + blockSize);
    }
    played.resize(static_cast<std::size_t>(length));
    return played;
}

} // namespace

TEST_CASE("Metronome clicks fall on the exact sample of the tempo map's beats",
          "[controller][metronome]")
{
    const double tempo = GENERATE(120.0, 97.3, 133.33);
    const auto signature = GENERATE(daw::model::TimeSignature{4, 4},
                                    daw::model::TimeSignature{7, 8});
    const double rate = GENERATE(44100.0, 48000.0, 96000.0);
    const int blockSize = GENERATE(64, 441, 512);
    CAPTURE(tempo, signature.numerator, signature.denominator, rate, blockSize);

    Project project;
    project.setTempo(tempo);
    project.setTimeSignature(signature);
    project.setMetronomeEnabled(true);
    project.setMetronomeVolume(metronomeGain);

    const auto length = static_cast<int>(rate * 4.0);
    Engine engine;
    engine.prepare(rate, blockSize);
    engine.setArrangement(clickingArrangement(project, rate, length * 2));
    engine.play();

    std::size_t heapOperations = 0;
    const auto played = play(engine, length, blockSize, heapOperations);

    CHECK(heapOperations == 0);
    CHECK(played == expectedClicks(project.getTempoMap(), rate, 0, length));
    // Clicks are not part of the mix: the master meter does not see them.
    CHECK(engine.takeOutputPeak(0) == 0.0f);
}

TEST_CASE("The engine's beat grid matches the tempo map around the start of the timeline",
          "[controller][metronome]")
{
    const TempoMap tempoMap(97.3, {7, 8});
    const daw::controller::TempoBeatGrid grid(tempoMap, 44100.0);

    for (juce::int64 beat = -50; beat < 50; ++beat)
    {
        const auto bar = beat >= 0 ? beat / 7 : (beat - 6) / 7;
        const auto beatInBar = static_cast<int>(beat - bar * 7);
        CHECK(grid.beatPosition(beat) ==
              ticksToSamples(tempoMap.beatStart(bar, beatInBar), 44100.0));
        CHECK(grid.firstBeatFrom(grid.beatPosition(beat)) == beat);
        CHECK(grid.firstBeatFrom(grid.beatPosition(beat) + 1) == beat + 1);
    }
}

TEST_CASE("The count-in clicks whole bars before playback, the timeline silent",
          "[controller][metronome]")
{
    constexpr double rate = 48000.0;
    constexpr juce::int64 bar = 96000; // 120 BPM in 4/4
    const juce::int64 start = GENERATE(juce::int64{0}, 2 * bar, 2 * bar + 1234);
    const int bars = GENERATE(1, 2);
    CAPTURE(start, bars);

    Project project; // metronome off: the count-in clicks anyway
    project.setMetronomeVolume(metronomeGain);
    const auto countInTicks = daw::controller::countInStart(
        project.getTempoMap(), daw::model::samplesToTicks(start, rate), bars);
    const auto countInLength = start - ticksToSamples(countInTicks, rate);
    CHECK(countInLength == bars * bar);

    // A constant clip under everything, to show that the timeline stays silent.
    const auto clip = daw::test::makeConstantSource(2, static_cast<int>(10 * bar), 0.25f, rate);
    auto arrangement = clickingArrangement(project, rate, 10 * bar);
    const auto withClip = daw::test::makeWholeFileArrangement(clip);
    arrangement->tracks = withClip->tracks;
    arrangement->sources = withClip->sources;

    Engine engine;
    engine.prepare(rate, 512);
    engine.setArrangement(std::move(arrangement));
    engine.playWithCountIn(start, countInLength);

    std::size_t heapOperations = 0;
    play(engine, 512, 512, heapOperations);
    CHECK(engine.isCountingIn());
    const auto rest = play(engine, static_cast<int>(countInLength) + 48000 - 512, 512,
                           heapOperations);
    CHECK(heapOperations == 0);
    CHECK_FALSE(engine.isCountingIn());

    std::vector<float> played;
    {
        Engine again; // the same from the beginning, in one piece
        again.prepare(rate, 512);
        auto copy = clickingArrangement(project, rate, 10 * bar);
        copy->tracks = withClip->tracks;
        copy->sources = withClip->sources;
        again.setArrangement(std::move(copy));
        again.playWithCountIn(start, countInLength);
        played = play(again, static_cast<int>(countInLength) + 48000, 512, heapOperations);
    }
    CHECK(std::equal(rest.begin(), rest.end(), played.begin() + 512));

    // Clicks only, on the beats before the start; then playback from the start with
    // the transport's fade-in, plus whatever is left of the last click. No click on
    // the start itself: the metronome is off.
    const auto clicks = expectedClicks(project.getTempoMap(), rate, start - countInLength,
                                       static_cast<int>(countInLength) + 48000, start);
    const auto split = played.begin() + countInLength;
    CHECK(std::equal(played.begin(), split, clicks.begin()));

    bool playsFromStart = true;
    for (std::size_t i = 0; i < 48000; ++i)
    {
        const float fade = i < 240 ? static_cast<float>(i + 1) / 240.0f : 1.0f;
        const auto index = static_cast<std::size_t>(countInLength) + i;
        playsFromStart = playsFromStart && played[index] == 0.25f * fade + clicks[index];
    }
    CHECK(playsFromStart);

    engine.stop();
    play(engine, 512, 512, heapOperations);
    CHECK(engine.getPlayheadPosition() == start);
}

TEST_CASE("Exports never click", "[controller][metronome]")
{
    Project project;
    project.setMetronomeEnabled(true);
    const auto arrangement = clickingArrangement(project, 48000.0, 96000);

    daw::engine::OfflineRenderer renderer(*arrangement);
    juce::AudioBuffer<float> output(2, 96000);
    renderer.render(output);
    CHECK(output.getMagnitude(0, 96000) == 0.0f);
}
