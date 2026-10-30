#include "daw/engine/Engine.h"

#include "support/RealtimeCheck.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Engine remembers the stream format it was prepared with", "[engine]")
{
    daw::engine::Engine engine;
    engine.prepare(44100.0, 256);

    CHECK(engine.getSampleRate() == 44100.0);
    CHECK(engine.getMaximumBlockSize() == 256);
}

TEST_CASE("Engine overwrites the output with silence when nothing is loaded", "[engine]")
{
    constexpr int numSamples = 512;

    daw::engine::Engine engine;
    engine.prepare(48000.0, numSamples);

    juce::AudioBuffer<float> buffer(2, numSamples);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), 0.5f, numSamples);

    const auto heapOperations = daw::test::heapOperationsDuring([&] { engine.process(buffer); });

    CHECK(heapOperations == 0);
    CHECK(buffer.getMagnitude(0, numSamples) == 0.0f);
}
