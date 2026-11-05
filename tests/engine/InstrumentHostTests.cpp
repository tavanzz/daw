#include "daw/engine/fx/EffectInstance.h"
#include "daw/engine/midi/MidiEvent.h"

#include "support/RealtimeCheck.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <limits>
#include <memory>
#include <vector>

namespace midi = daw::engine::midi;
using daw::engine::fx::EffectInstance;
using daw::engine::fx::EffectScratch;
using daw::engine::fx::ProcessSetup;
using daw::engine::fx::SlotContext;
using daw::engine::fx::SlotSettings;
using daw::test::TestInstrument;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int fadeLength = 480; // 10 ms

// Events at timeline samples, handed to the instance in blocks of a given size, as
// the mixer does: each block gets the events that fall inside it.
struct InstrumentRunner
{
    explicit InstrumentRunner(int maximumBlock = 512, bool fadeIn = false)
        : probe(new TestInstrument())
        , instance(std::unique_ptr<daw::engine::fx::Effect>(probe), setupWith(maximumBlock),
                   fadeIn)
    {
        scratch.prepare(maximumBlock);
        blockEvents.reserve(4096);
    }

    static ProcessSetup setupWith(int maximumBlock)
    {
        ProcessSetup setup;
        setup.sampleRate = sampleRate;
        setup.maximumBlockSize = maximumBlock;
        return setup;
    }

    // Runs length samples from the current position; events' offsets are positions.
    std::vector<float> run(const std::vector<midi::Event>& events, int length, int blockSize)
    {
        std::vector<float> left(static_cast<std::size_t>(length));
        std::vector<float> right(static_cast<std::size_t>(length));
        heapOperations += daw::test::heapOperationsDuring([&] {
            for (int done = 0; done < length;)
            {
                const int piece = std::min(blockSize, length - done);
                blockEvents.clear();
                for (auto event : events)
                    if (event.offset >= position + done && event.offset < position + done + piece)
                    {
                        event.offset -= position + done;
                        blockEvents.push_back(event);
                    }
                context.events = blockEvents;
                float* const channels[] = {left.data() + done, right.data() + done};
                instance.process(channels, piece, settings, context, scratch);
                done += piece;
            }
        });
        position += length;
        rightChannel = right;
        return left;
    }

    TestInstrument* probe;
    EffectInstance instance;
    EffectScratch scratch;
    SlotSettings settings;
    SlotContext context;
    std::vector<midi::Event> blockEvents;
    std::vector<float> rightChannel;
    int position = 0;
    std::size_t heapOperations = 0;
};

} // namespace

TEST_CASE("Instruments get every event on its exact sample whatever the block size",
          "[engine][midi]")
{
    const int maximumBlock = GENERATE(64, 512);
    const int blockSize = GENERATE(1, 7, 64, 100, 512);
    if (blockSize > maximumBlock * 2)
        return;

    InstrumentRunner runner(maximumBlock);
    const std::vector<midi::Event> events{
        midi::noteOn(0, 60, 0.5f),     midi::noteOn(63, 62, 0.25f),
        midi::noteOff(64, 60),         midi::noteOn(64, 64, 0.125f),
        midi::controller(300, 1, 0.5f), midi::noteOff(511, 62),
        midi::noteOff(512, 64),        midi::noteOn(1000, 67, 1.0f),
        midi::noteOff(1999, 67)};

    const auto left = runner.run(events, 2000, blockSize);
    REQUIRE(runner.heapOperations == 0);

    // The level of every sample is the sum of the velocities held at that sample.
    for (int i = 0; i < 2000; ++i)
    {
        float expected = 0.0f;
        if (i < 64)
            expected += 0.5f;
        if (i >= 63 && i < 511)
            expected += 0.25f;
        if (i >= 64 && i < 512)
            expected += 0.125f;
        if (i >= 1000 && i < 1999)
            expected += 1.0f;
        REQUIRE(left[static_cast<std::size_t>(i)] == expected);
    }

    REQUIRE(runner.probe->received.size() == events.size());
    for (std::size_t k = 0; k < events.size(); ++k)
    {
        REQUIRE(runner.probe->received[k].sample == events[k].offset);
        REQUIRE(runner.probe->received[k].event.type == events[k].type);
        REQUIRE(runner.probe->received[k].event.number == events[k].number);
    }
    REQUIRE(runner.probe->wellFormed);
}

TEST_CASE("Bypassing an instrument ends its notes at once and gives it nothing while off",
          "[engine][midi]")
{
    InstrumentRunner runner;
    runner.run({midi::noteOn(0, 60, 0.5f), midi::noteOn(10, 64, 0.5f)}, 100, 64);
    REQUIRE(runner.instance.getHeldNotes().size() == 2);

    runner.settings.enabled = false;
    const auto left = runner.run({midi::noteOn(150, 67, 0.5f), midi::noteOff(160, 60)}, 2000, 64);

    // Both note-offs at the first bypassed sample, then nothing more.
    REQUIRE(runner.instance.getHeldNotes().isEmpty());
    REQUIRE(runner.probe->getHeldCount() == 0);
    REQUIRE(runner.probe->received.size() == 4);
    REQUIRE(runner.probe->received[2].sample == 100);
    REQUIRE(runner.probe->received[2].event.type == midi::EventType::noteOff);
    REQUIRE(runner.probe->received[3].sample == 100);
    REQUIRE(runner.probe->wellFormed);
    for (float sample : left)
        REQUIRE(sample == 0.0f);

    // Switched back on, it starts clean and plays new notes on time.
    const int resets = runner.probe->resets;
    runner.settings.enabled = true;
    const auto after = runner.run({midi::noteOn(2200, 72, 1.0f)}, 1000, 64);
    REQUIRE(runner.probe->resets == resets + 1);
    REQUIRE(runner.probe->received.back().event.number == 72);
    REQUIRE(after[99] == 0.0f);
    REQUIRE(after[100] > 0.0f);
    REQUIRE(after[100 + fadeLength] == 1.0f);
}

TEST_CASE("Instruments only get note-offs for notes they hold", "[engine][midi]")
{
    InstrumentRunner runner;
    runner.run({midi::noteOff(5, 60), midi::noteOn(10, 60, 0.5f, 0, 3),
                midi::noteOff(20, 60, 0, 4), midi::noteOff(30, 60, 0, 3)},
               100, 32);

    REQUIRE(runner.probe->wellFormed);
    REQUIRE(runner.probe->received.size() == 2);
    REQUIRE(runner.probe->received[1].sample == 30);

    // A full table refuses further notes, and their note-offs.
    std::vector<midi::Event> many;
    for (int i = 0; i < midi::HeldNotes::capacity + 10; ++i)
        many.push_back(midi::noteOn(100, i % 128, 0.001f, i / 128));
    for (int i = 0; i < midi::HeldNotes::capacity + 10; ++i)
        many.push_back(midi::noteOff(150, i % 128, i / 128));
    runner.run(many, 100, 512);
    REQUIRE(runner.heapOperations == 0);
    REQUIRE(runner.probe->wellFormed);
    REQUIRE(runner.probe->getHeldCount() == 0);
    REQUIRE(runner.probe->received.size() == 2 + 2 * midi::HeldNotes::capacity);
}

TEST_CASE("Effects never get events, instruments get them while fading in",
          "[engine][midi]")
{
    auto* gain = new daw::test::GainEffect();
    EffectInstance effect(std::unique_ptr<daw::engine::fx::Effect>(gain),
                          InstrumentRunner::setupWith(512));
    EffectScratch scratch;
    scratch.prepare(512);
    const std::vector<midi::Event> events{midi::noteOn(0, 60, 1.0f)};
    SlotContext context;
    context.events = events;
    std::vector<float> left(64, 1.0f);
    std::vector<float> right(64, 1.0f);
    float* const channels[] = {left.data(), right.data()};
    effect.process(channels, 64, SlotSettings{}, context, scratch);
    REQUIRE(effect.getHeldNotes().isEmpty());
    REQUIRE(left[0] == 1.0f);

    InstrumentRunner fading(512, true);
    const auto out = fading.run({midi::noteOn(0, 60, 1.0f)}, fadeLength * 2, 128);
    REQUIRE(out[0] > 0.0f);
    REQUIRE(out[0] < 0.01f);
    REQUIRE(out[fadeLength / 2] < 1.0f);
    REQUIRE(out[fadeLength] == 1.0f);
}

TEST_CASE("An instrument silenced for a NaN forgets its notes", "[engine][midi]")
{
    InstrumentRunner runner;
    runner.run({midi::noteOn(0, 60, 0.5f)}, 64, 64);
    runner.probe->outputsNaN = true;
    runner.run({}, 64, 64);
    REQUIRE(runner.instance.hasFailed());
    REQUIRE(runner.instance.getHeldNotes().isEmpty());

    runner.probe->outputsNaN = false;
    runner.settings.revival = 1;
    runner.run({midi::noteOn(200, 62, 0.5f)}, 2000, 64);
    REQUIRE_FALSE(runner.instance.hasFailed());
    REQUIRE(runner.probe->getHeldCount() == 1);
    REQUIRE(runner.probe->wellFormed);
}
