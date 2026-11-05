#include "daw/engine/Engine.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/NotePlayback.h"

#include "support/MidiArrangements.h"
#include "support/RealtimeCheck.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <vector>

namespace midi = daw::engine::midi;
using daw::engine::ArrangementNote;
using daw::engine::ControllerLane;
using daw::engine::Engine;
using daw::test::makeMidiArrangement;
using daw::test::makeNote;
using daw::test::makeTestInstrument;
using daw::test::runEngine;

namespace
{

constexpr double sampleRate = 48000.0;

// The sum of the velocities of the notes sounding at position, counting only the
// notes that started since passStart (notes are not restarted after a jump).
float expectedLevel(const std::vector<ArrangementNote>& notes, juce::int64 position,
                    juce::int64 passStart)
{
    // A note struck again on its key ends the one before it.
    float level = 0.0f;
    for (std::size_t i = 0; i < notes.size(); ++i)
    {
        const auto& note = notes[i];
        if (note.start < passStart || position < note.start || position >= note.end)
            continue;
        bool retriggered = false;
        for (const auto& other : notes)
            if (&other != &note && other.key == note.key && other.start > note.start &&
                other.start <= position)
                retriggered = true;
        if (!retriggered)
            level += note.velocity;
    }
    return level;
}

std::vector<std::vector<int>> blockPatterns()
{
    return {{256}, {1, 2, 3}, {7, 64, 100}, {512, 33}, {511}};
}

} // namespace

TEST_CASE("Notes start and end on their exact samples whatever the block size",
          "[engine][midi]")
{
    const auto blocks = GENERATE(Catch::Generators::from_range(blockPatterns()));

    const std::vector<ArrangementNote> notes{
        makeNote(100, 1000, 60, 0.5f),    makeNote(1000, 1500, 60, 0.25f),
        makeNote(300, 301, 62, 0.125f),   makeNote(255, 257, 64, 1.0f),
        makeNote(2000, 5000, 65, 0.0625f), makeNote(3000, 4000, 65, 0.03125f)};

    auto instrument = makeTestInstrument();
    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(makeMidiArrangement(instrument, notes, 6000));
    engine.play();

    const auto run = runEngine(engine, 7000, blocks);
    REQUIRE(run.heapOperations == 0);
    for (juce::int64 k = 0; k < 6000; ++k)
        REQUIRE(run.left[static_cast<std::size_t>(k)] == expectedLevel(notes, k, 0));
    for (std::size_t k = 6000; k < run.left.size(); ++k)
        REQUIRE(run.left[k] == 0.0f);

    REQUIRE(instrument.probe->wellFormed);
    REQUIRE(instrument.probe->getHeldCount() == 0);
    engine.collectGarbage();
}

TEST_CASE("Notes in a loop end at the wrap and start again on every pass", "[engine][midi]")
{
    const auto blocks = GENERATE(Catch::Generators::from_range(blockPatterns()));

    const std::vector<ArrangementNote> notes{
        makeNote(500, 1500, 60, 0.5f),  makeNote(2500, 3500, 62, 0.25f),
        makeNote(1000, 1200, 64, 0.125f), makeNote(2999, 3000, 65, 1.0f)};

    auto instrument = makeTestInstrument();
    auto arrangement = makeMidiArrangement(instrument, notes, 4000);
    arrangement->loopStart = 1000;
    arrangement->loopEnd = 3000;

    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(std::move(arrangement));
    engine.setLooping(true);
    engine.play();

    const auto run = runEngine(engine, 9000, blocks);
    REQUIRE(run.heapOperations == 0);
    for (juce::int64 k = 0; k < 9000; ++k)
    {
        const juce::int64 position = k < 3000 ? k : 1000 + (k - 3000) % 2000;
        const juce::int64 passStart = k < 3000 ? 0 : 1000;
        REQUIRE(run.left[static_cast<std::size_t>(k)] == expectedLevel(notes, position, passStart));
    }
    REQUIRE(instrument.probe->wellFormed);
    engine.collectGarbage();
}

TEST_CASE("Controllers are chased where playback starts and return to rest at stop",
          "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    auto arrangement = makeMidiArrangement(instrument, {makeNote(1200, 1300, 60, 0.5f)}, 10000);
    ControllerLane modWheel;
    modWheel.number = midi::controllers::modWheel;
    modWheel.steps = {{0, 0.25f}, {2000, 0.5f}};
    ControllerLane bend;
    bend.type = midi::EventType::pitchBend;
    bend.steps = {{1500, 0.5f}};
    ControllerLane volume;
    volume.number = midi::controllers::volume;
    volume.steps = {{0, 1.0f}}; // at rest: nothing to send
    arrangement->tracks[0].controllers = {modWheel, bend, volume};

    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(std::move(arrangement));
    engine.seek(1000);
    engine.play();
    runEngine(engine, 3000, {256});
    engine.stop();
    runEngine(engine, 512, {256});

    const auto& received = instrument.probe->received;
    REQUIRE(received.size() == 7);
    // At the first sample: the mod wheel's value before 1000.
    CHECK(received[0].sample == 0);
    CHECK(received[0].event == midi::controller(0, 1, 0.25f));
    CHECK(received[1].sample == 200);
    CHECK(received[1].event.type == midi::EventType::noteOn);
    CHECK(received[2].sample == 300);
    CHECK(received[2].event.type == midi::EventType::noteOff);
    CHECK(received[3].sample == 500);
    CHECK(received[3].event == midi::pitchBend(received[3].event.offset, 0.5f));
    CHECK(received[4].sample == 1000);
    CHECK(received[4].event == midi::controller(received[4].event.offset, 1, 0.5f));
    // Stopped at stream sample 3000 (the next block's start): back to rest.
    CHECK(received[5].sample == 3000);
    CHECK(received[6].sample == 3000);
    for (std::size_t i = 5; i < 7; ++i)
        CHECK(received[i].event.value == 0.0f);
    engine.collectGarbage();
}

TEST_CASE("Note playback allocates nothing with many notes at once", "[engine][midi]")
{
    std::vector<ArrangementNote> notes;
    for (int i = 0; i < 400; ++i)
        notes.push_back(makeNote(i, 2000 + i, i % 128, 1.0f / 1024.0f));
    for (int i = 0; i < 3000; ++i)
        notes.push_back(makeNote(3000 + i, 3001 + i, i % 128, 1.0f / 1024.0f));

    auto instrument = makeTestInstrument();
    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(makeMidiArrangement(instrument, notes, 8000));
    engine.play();
    const auto run = runEngine(engine, 8000, {512, 13});
    REQUIRE(run.heapOperations == 0);

    // Retriggered keys end their earlier notes; no more than the capacity sound.
    REQUIRE(instrument.probe->wellFormed);
    REQUIRE(instrument.probe->getHeldCount() == 0);
    float loudest = 0.0f;
    for (float sample : run.left)
        loudest = std::max(loudest, sample);
    REQUIRE(loudest <= 128.0f / 1024.0f);
    engine.collectGarbage();
}
