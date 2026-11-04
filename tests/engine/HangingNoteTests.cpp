#include "daw/engine/Engine.h"
#include "daw/engine/OfflineRenderer.h"

#include "support/MidiArrangements.h"
#include "support/RealtimeCheck.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <memory>
#include <vector>

namespace midi = daw::engine::midi;
using daw::engine::Arrangement;
using daw::engine::ArrangementNote;
using daw::engine::ControllerLane;
using daw::engine::Engine;
using daw::engine::OfflineRenderer;
using daw::test::HostedInstrument;
using daw::test::makeMidiArrangement;
using daw::test::makeNote;
using daw::test::makeTestInstrument;
using daw::test::runEngine;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int fadeLength = 240; // the transport's 5 ms

// How many note-ons with this key the instrument received.
int noteOnsOf(const HostedInstrument& instrument, int key)
{
    int count = 0;
    for (const auto& received : instrument.probe->received)
        count += received.event.type == midi::EventType::noteOn && received.event.number == key;
    return count;
}

// The sample of the last event of this type and key the instrument received; -1 if none.
juce::int64 lastSampleOf(const HostedInstrument& instrument, midi::EventType type, int key)
{
    juce::int64 sample = -1;
    for (const auto& received : instrument.probe->received)
        if (received.event.type == type && received.event.number == key)
            sample = received.sample;
    return sample;
}

bool allSilent(const std::vector<float>& samples, std::size_t from, std::size_t to)
{
    for (std::size_t i = from; i < to && i < samples.size(); ++i)
        if (samples[i] != 0.0f)
            return false;
    return true;
}

std::shared_ptr<daw::engine::fx::EffectInstance> hosted(std::unique_ptr<daw::engine::fx::Effect> effect,
                                                        int maximumBlock = 512)
{
    daw::engine::fx::ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = maximumBlock;
    return std::make_shared<daw::engine::fx::EffectInstance>(std::move(effect), setup);
}

} // namespace

TEST_CASE("Stop and pause end every sounding note where they happen", "[engine][midi]")
{
    const bool pause = GENERATE(false, true);
    auto instrument = makeTestInstrument();
    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(makeMidiArrangement(
        instrument, {makeNote(0, 100000, 60, 0.5f), makeNote(10, 90000, 64, 0.25f)}, 100000));
    engine.play();
    runEngine(engine, 1000, {250});
    REQUIRE(instrument.probe->getHeldCount() == 2);

    if (pause)
        engine.pause();
    else
        engine.stop();
    const auto after = runEngine(engine, 1000, {250});
    REQUIRE(after.heapOperations == 0);
    REQUIRE(instrument.probe->getHeldCount() == 0);
    REQUIRE(lastSampleOf(instrument, midi::EventType::noteOff, 60) == 1000);
    REQUIRE(allSilent(after.left, 0, after.left.size()));
    REQUIRE(instrument.probe->wellFormed);

    // Playing again after a pause does not restart notes that began earlier; after a
    // stop playback starts again from 0, where the note begins.
    engine.play();
    runEngine(engine, 1000, {250});
    REQUIRE(noteOnsOf(instrument, 60) == (pause ? 1 : 2));
    engine.stop();
    runEngine(engine, 500, {250});
    REQUIRE(instrument.probe->getHeldCount() == 0);
    engine.collectGarbage();
}

TEST_CASE("A seek while playing ends the notes as it fades out, then plays from there",
          "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(makeMidiArrangement(
        instrument, {makeNote(0, 100000, 60, 0.5f), makeNote(50000, 51000, 62, 0.25f)}, 100000));
    engine.play();
    runEngine(engine, 1000, {200});
    engine.seek(50000);
    const auto after = runEngine(engine, 3000, {200});

    REQUIRE(allSilent(after.left, 0, fadeLength));
    for (std::size_t i = fadeLength; i < fadeLength + 1000; ++i)
        REQUIRE(after.left[i] == 0.25f);
    REQUIRE(allSilent(after.left, fadeLength + 1000, after.left.size()));
    REQUIRE(instrument.probe->getHeldCount() == 0);
    REQUIRE(instrument.probe->wellFormed);
    engine.collectGarbage();
}

TEST_CASE("Edits while playing end removed notes at once and keep the others going",
          "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    const std::vector<ArrangementNote> before{
        makeNote(0, 10000, 60, 0.5f, 1),  makeNote(0, 10000, 62, 0.25f, 2),
        makeNote(0, 10000, 64, 0.125f, 3), makeNote(0, 3000, 65, 0.0625f, 4),
        makeNote(0, 10000, 67, 1.0f, 5)};
    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(makeMidiArrangement(instrument, before, 20000));
    engine.play();
    runEngine(engine, 2000, {250});

    // Note 2 removed, 3 shortened, 4 lengthened, 5 moved later; 1 unchanged.
    const std::vector<ArrangementNote> after{
        makeNote(0, 10000, 60, 0.5f, 1), makeNote(0, 2500, 64, 0.125f, 3),
        makeNote(0, 6000, 65, 0.0625f, 4), makeNote(4000, 5000, 67, 1.0f, 5)};
    engine.setArrangement(makeMidiArrangement(instrument, after, 20000));
    const auto run = runEngine(engine, 10000, {250});
    REQUIRE(run.heapOperations == 0);

    CHECK(noteOnsOf(instrument, 60) == 1);
    CHECK(lastSampleOf(instrument, midi::EventType::noteOff, 60) == 10000);
    CHECK(lastSampleOf(instrument, midi::EventType::noteOff, 62) == 2000);
    CHECK(lastSampleOf(instrument, midi::EventType::noteOff, 64) == 2500);
    CHECK(noteOnsOf(instrument, 65) == 1);
    CHECK(lastSampleOf(instrument, midi::EventType::noteOff, 65) == 6000);
    CHECK(noteOnsOf(instrument, 67) == 2);
    CHECK(lastSampleOf(instrument, midi::EventType::noteOn, 67) == 4000);
    CHECK(lastSampleOf(instrument, midi::EventType::noteOff, 67) == 5000);
    CHECK(instrument.probe->getHeldCount() == 0);
    CHECK(instrument.probe->wellFormed);
    engine.collectGarbage();
}

TEST_CASE("Muting, bypassing or removing a MIDI track ends its notes", "[engine][midi]")
{
    const int how = GENERATE(0, 1, 2);
    auto instrument = makeTestInstrument();
    const std::vector<ArrangementNote> notes{makeNote(0, 100000, 60, 0.5f),
                                             makeNote(0, 100000, 64, 0.25f)};
    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(makeMidiArrangement(instrument, notes, 100000));
    engine.play();
    runEngine(engine, 2000, {250});
    REQUIRE(instrument.probe->getHeldCount() == 2);

    auto changed = makeMidiArrangement(instrument, notes, 100000);
    if (how == 0)
    {
        // Muted: the track plays no notes (and its gains go to 0).
        changed->tracks[0].notes.clear();
        changed->tracks[0].leftGain = 0.0f;
        changed->tracks[0].rightGain = 0.0f;
    }
    else if (how == 1)
    {
        changed->tracks[0].inserts.slots[0].enabled = false;
    }
    else
    {
        changed->tracks.clear();
    }
    engine.setArrangement(std::move(changed));
    const auto run = runEngine(engine, 2000, {250});
    REQUIRE(run.heapOperations == 0);

    CAPTURE(how);
    REQUIRE(instrument.probe->getHeldCount() == 0);
    REQUIRE(lastSampleOf(instrument, midi::EventType::noteOff, 60) == 2000);
    REQUIRE(lastSampleOf(instrument, midi::EventType::noteOff, 64) == 2000);
    REQUIRE(instrument.probe->wellFormed);
    engine.collectGarbage();
}

TEST_CASE("A held sustain pedal lets go of the notes at the loop's wrap", "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    auto arrangement = makeMidiArrangement(instrument, {makeNote(500, 1500, 60, 0.5f)}, 2000);
    ControllerLane sustain;
    sustain.number = midi::controllers::sustain;
    sustain.steps = {{0, 1.0f}};
    arrangement->tracks[0].controllers = {sustain};
    arrangement->loopStart = 0;
    arrangement->loopEnd = 1000;

    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(std::move(arrangement));
    engine.setLooping(true);
    engine.play();
    runEngine(engine, 1500, {256});

    // At the wrap (sample 1000): the note ends, the pedal goes up and down again.
    std::vector<midi::Event> atWrap;
    for (const auto& received : instrument.probe->received)
        if (received.sample == 1000)
            atWrap.push_back(received.event);
    REQUIRE(atWrap.size() == 3);
    CHECK(atWrap[0].type == midi::EventType::noteOff);
    CHECK(atWrap[1] == midi::controller(0, midi::controllers::sustain, 0.0f));
    CHECK(atWrap[2] == midi::controller(0, midi::controllers::sustain, 1.0f));
    engine.collectGarbage();
}

TEST_CASE("Notes are silent during the count-in and start on time after it", "[engine][midi]")
{
    auto instrument = makeTestInstrument();
    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(makeMidiArrangement(
        instrument, {makeNote(500, 1500, 60, 0.5f), makeNote(1000, 1100, 62, 0.25f)}, 5000));
    engine.playWithCountIn(1000, 2000);
    const auto run = runEngine(engine, 4000, {300});

    REQUIRE(allSilent(run.left, 0, 2000));
    for (std::size_t i = 2000; i < 2100; ++i)
        REQUIRE(run.left[i] == 0.25f);
    REQUIRE(allSilent(run.left, 2100, run.left.size()));
    REQUIRE(noteOnsOf(instrument, 60) == 0);
    engine.collectGarbage();
}

TEST_CASE("The export equals real-time playback with instruments and automation",
          "[engine][midi][bounce]")
{
    const auto makeSong = [](const HostedInstrument& instrument) {
        std::vector<ArrangementNote> notes;
        for (int i = 0; i < 60; ++i)
            notes.push_back(makeNote(300 + i * 457, 300 + i * 457 + 200 + (i * 97) % 1500,
                                     40 + (i * 7) % 40, 0.1f + 0.01f * static_cast<float>(i % 13)));
        auto song = makeMidiArrangement(instrument, notes, 30000);
        auto& track = song->tracks[0];
        track.inserts.slots[0].automation = {std::make_shared<const daw::engine::AutomationCurve>(
            std::vector<daw::engine::AutomationPoint>{{0, 0.2f, 0.0f}, {25000, 0.9f, 0.3f}})};

        daw::engine::ChainSlot latency;
        latency.instance = hosted(std::make_unique<daw::test::LatencyEffect>(64));
        track.inserts.slots.push_back(latency);
        daw::engine::ChainSlot gain;
        gain.instance = hosted(std::make_unique<daw::test::GainEffect>(1.0f));
        gain.automation = {std::make_shared<const daw::engine::AutomationCurve>(
            std::vector<daw::engine::AutomationPoint>{{5000, 0.9f, 0.0f}, {15000, 0.4f, -0.5f}})};
        track.inserts.slots.push_back(gain);
        track.automation.panCurve = std::make_shared<const daw::engine::AutomationCurve>(
            std::vector<daw::engine::AutomationPoint>{{0, 0.1f, 0.0f}, {30000, 0.9f, 0.0f}});
        return song;
    };

    auto forExport = makeTestInstrument(sampleRate, OfflineRenderer::defaultBlockSize);
    const auto song = makeSong(forExport);
    OfflineRenderer renderer(*song);
    juce::AudioBuffer<float> exported(2, 30000);
    renderer.render(exported);
    const int latency = renderer.getLatency();
    REQUIRE(latency == 64);

    const int blockSize = GENERATE(64, 441);
    auto forPlayback = makeTestInstrument(sampleRate, blockSize);
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(makeSong(forPlayback));
    engine.play();

    std::vector<float> left;
    std::vector<float> right;
    juce::AudioBuffer<float> block(2, blockSize);
    std::size_t heapOperations = 0;
    while (left.size() < static_cast<std::size_t>(30000 + latency))
    {
        heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
        left.insert(left.end(), block.getReadPointer(0), block.getReadPointer(0) + blockSize);
        right.insert(right.end(), block.getReadPointer(1), block.getReadPointer(1) + blockSize);
    }

    bool identical = true;
    for (int k = 0; k < 30000; ++k)
    {
        const auto s = static_cast<std::size_t>(k + latency);
        identical = identical &&
                    std::bit_cast<std::uint32_t>(left[s]) ==
                        std::bit_cast<std::uint32_t>(exported.getSample(0, k)) &&
                    std::bit_cast<std::uint32_t>(right[s]) ==
                        std::bit_cast<std::uint32_t>(exported.getSample(1, k));
    }
    CAPTURE(blockSize);
    CHECK(identical);
    CHECK(exported.getMagnitude(0, 0, 30000) > 0.05f);
    CHECK(heapOperations == 0);
    engine.collectGarbage();
}
