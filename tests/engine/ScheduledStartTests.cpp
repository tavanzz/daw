#include "daw/engine/Engine.h"

#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>

#include <vector>

using daw::engine::Engine;
using daw::engine::TransportState;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int fadeLength = 240; // the transport's 5 ms fade-in

// A clip whose sample n is n + 1, so the output says which sample of it plays.
std::unique_ptr<daw::engine::Arrangement> ramp(int length)
{
    juce::AudioBuffer<float> samples(1, length);
    for (int n = 0; n < length; ++n)
        samples.setSample(0, n, static_cast<float>(n + 1));
    return daw::test::makeWholeFileArrangement(daw::test::makeSource(samples, sampleRate));
}

struct Output
{
    std::vector<float> left;
    std::size_t heapOperations = 0;
};

// Renders up to `total` samples in blocks of the given sizes (repeated), the start
// scheduled after the first block.
Output render(const std::vector<int>& blockSizes, int total, juce::int64 position,
              juce::int64 startAt)
{
    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(ramp(20000));

    Output output;
    juce::AudioBuffer<float> block(2, 512);
    std::size_t index = 0;
    bool scheduled = false;
    while (static_cast<int>(output.left.size()) < total)
    {
        const int size = blockSizes[index++ % blockSizes.size()];
        juce::AudioBuffer<float> piece(block.getArrayOfWritePointers(), 2, size);
        output.heapOperations += daw::test::heapOperationsDuring([&] { engine.process(piece); });
        output.left.insert(output.left.end(), piece.getReadPointer(0), piece.getReadPointer(0) + size);
        if (!scheduled)
        {
            engine.playAt(position, startAt);
            scheduled = true;
        }
    }
    output.left.resize(static_cast<std::size_t>(total));
    return output;
}

} // namespace

TEST_CASE("A scheduled start begins on its exact sample whatever the blocks", "[engine]")
{
    constexpr juce::int64 startAt = 3000;
    constexpr juce::int64 position = 1000;

    const auto even = render({512}, 8000, position, startAt);
    const auto odd = render({64, 100, 333, 7, 512}, 8000, position, startAt);
    CHECK(even.heapOperations == 0);
    CHECK(odd.heapOperations == 0);
    CHECK(even.left == odd.left);

    for (std::size_t n = 0; n < static_cast<std::size_t>(startAt); ++n)
        REQUIRE(even.left[n] == 0.0f);
    // After the fade-in, sample k after the start is the clip's sample position + k.
    for (int k = fadeLength; k < 4000; ++k)
        REQUIRE(even.left[static_cast<std::size_t>(startAt + k)]
                == static_cast<float>(position + k + 1));
    CHECK(even.left[static_cast<std::size_t>(startAt) + 1] > 0.0f);
}

TEST_CASE("A scheduled start already due starts at once and others cancel it", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, 256);
    engine.setArrangement(ramp(20000));
    juce::AudioBuffer<float> block(2, 256);
    engine.process(block);

    engine.playAt(500, 0); // in the past
    engine.process(block);
    CHECK(engine.getTransportState() == TransportState::playing);
    CHECK_FALSE(engine.isStartScheduled());

    // Playback that runs pauses until the start.
    engine.playAt(0, 100000);
    engine.process(block);
    CHECK(engine.isStartScheduled());
    CHECK(engine.getTransportState() == TransportState::paused);
    engine.stop();
    for (int i = 0; i < 400; ++i)
        engine.process(block);
    CHECK_FALSE(engine.isStartScheduled());
    CHECK(engine.getTransportState() == TransportState::stopped);
}

TEST_CASE("The engine publishes where its stream stands and when", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, 256);
    juce::AudioBuffer<float> block(2, 256);
    daw::engine::AudioInputs inputs;

    inputs.time = 12.5;
    engine.process(inputs, block);
    inputs.time = 12.5 + 256.0 / sampleRate;
    engine.process(inputs, block);

    const auto clock = engine.getStreamClock();
    CHECK(clock.stream == 256);
    CHECK(clock.time == inputs.time);
    CHECK(clock.sampleRate == sampleRate);
}
