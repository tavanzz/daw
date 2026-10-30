#include "daw/engine/Engine.h"

#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using Catch::Matchers::WithinAbs;
using daw::engine::Arrangement;
using daw::engine::Engine;
using daw::engine::TransportState;
using daw::test::makeConstantSource;
using daw::test::makeWholeFileArrangement;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr int fadeLength = 240; // 5 ms at 48 kHz, for both the transport and crossfades

// Runs numBlocks audio blocks the way the device callback would and returns
// output channel 0, plus the heap operations made inside process().
struct Rendered
{
    std::vector<float> samples;
    std::size_t heapOperations = 0;
};

Rendered process(Engine& engine, int numBlocks)
{
    Rendered rendered;
    juce::AudioBuffer<float> block(2, blockSize);

    for (int i = 0; i < numBlocks; ++i)
    {
        rendered.heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
        rendered.samples.insert(rendered.samples.end(), block.getReadPointer(0),
                                block.getReadPointer(0) + blockSize);
    }

    return rendered;
}

std::unique_ptr<Arrangement> constant(int length, float value, double rate = sampleRate,
                                      juce::int64 trackId = 1)
{
    return makeWholeFileArrangement(makeConstantSource(2, length, value, rate), trackId);
}

float largestStep(const std::vector<float>& samples)
{
    float largest = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    return largest;
}

} // namespace

TEST_CASE("Engine remembers the stream format it was prepared with", "[engine]")
{
    Engine engine;
    engine.prepare(44100.0, 256);

    CHECK(engine.getSampleRate() == 44100.0);
    CHECK(engine.getMaximumBlockSize() == 256);
}

TEST_CASE("Engine overwrites the output with silence when nothing is loaded", "[engine]")
{
    constexpr int numSamples = 512;

    Engine engine;
    engine.prepare(48000.0, numSamples);

    juce::AudioBuffer<float> buffer(2, numSamples);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        juce::FloatVectorOperations::fill(buffer.getWritePointer(channel), 0.5f, numSamples);

    engine.play();
    const auto heapOperations = daw::test::heapOperationsDuring([&] { engine.process(buffer); });

    CHECK(heapOperations == 0);
    CHECK(buffer.getMagnitude(0, numSamples) == 0.0f);
    CHECK(engine.getTransportState() == TransportState::stopped);
}

TEST_CASE("Engine plays an arrangement once told to, from the next block", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(constant(48000, 0.5f));

    (void) process(engine, 1);
    CHECK(engine.getLength() == 48000);
    CHECK(engine.getTransportState() == TransportState::stopped);

    engine.play();
    CHECK(engine.getTransportState() == TransportState::stopped); // not processed yet

    const auto rendered = process(engine, 10);
    CHECK(rendered.heapOperations == 0);
    CHECK(engine.getTransportState() == TransportState::playing);
    CHECK(engine.getPlayheadPosition() == 10 * blockSize);
    CHECK(rendered.samples.back() == 0.5f);
}

TEST_CASE("A play sent right after a new arrangement applies to it", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(constant(48000, 0.5f));
    engine.play();

    const auto rendered = process(engine, 2);
    CHECK(engine.getTransportState() == TransportState::playing);
    CHECK(rendered.samples.back() == 0.5f);
}

TEST_CASE("Engine publishes pause, stop, seek and loop", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(constant(48000, 0.5f));
    engine.play();
    (void) process(engine, 4);

    engine.pause();
    (void) process(engine, 2);
    CHECK(engine.getTransportState() == TransportState::paused);
    const auto pausedAt = engine.getPlayheadPosition();
    (void) process(engine, 2);
    CHECK(engine.getPlayheadPosition() == pausedAt);

    engine.seek(12345);
    engine.setLooping(true);
    (void) process(engine, 1);
    CHECK(engine.getPlayheadPosition() == 12345);
    CHECK(engine.isLooping());

    // Stop goes back to where the user last moved the playhead, stop again to 0.
    engine.stop();
    (void) process(engine, 1);
    CHECK(engine.getTransportState() == TransportState::stopped);
    CHECK(engine.getPlayheadPosition() == 12345);
    engine.stop();
    (void) process(engine, 1);
    CHECK(engine.getPlayheadPosition() == 0);
}

TEST_CASE("Engine plays silence for an arrangement at another sample rate", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(constant(48000, 0.5f, 44100.0));
    engine.play();

    const auto rendered = process(engine, 10);
    CHECK(std::all_of(rendered.samples.begin(), rendered.samples.end(),
                      [](float value) { return value == 0.0f; }));
}

TEST_CASE("A different arrangement while playing is crossfaded in", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(constant(48000, 0.5f));
    engine.play();
    (void) process(engine, 10);

    // Another clip under the playhead: the old audio fades out as the new fades in.
    engine.setArrangement(constant(48000, -0.25f, sampleRate, 1));
    const auto rendered = process(engine, 4);

    CHECK(rendered.heapOperations == 0);
    CHECK_THAT(rendered.samples[0], WithinAbs(0.5 - 0.75 / fadeLength, 1e-6));
    CHECK(rendered.samples[fadeLength - 1] == -0.25f);
    CHECK(rendered.samples.back() == -0.25f);
    CHECK(largestStep(rendered.samples) <= 0.75f / fadeLength * 1.001f);
    CHECK(engine.getTransportState() == TransportState::playing);
    CHECK(engine.getPlayheadPosition() == 14 * blockSize); // playback never paused
}

TEST_CASE("A gain change while playing ramps instead of crossfading", "[engine]")
{
    const auto source = makeConstantSource(2, 96000, 1.0f, sampleRate);

    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(makeWholeFileArrangement(source, 1, 1.0f));
    engine.play();
    (void) process(engine, 2);

    engine.setArrangement(makeWholeFileArrangement(source, 1, 0.5f));
    const auto rendered = process(engine, 8);

    // 20 ms ramp: 960 samples from 1 to 0.5.
    CHECK_THAT(rendered.samples[0], WithinAbs(1.0 - 0.5 / 960.0, 1e-6));
    CHECK(rendered.samples[479] == 0.75f);
    CHECK(rendered.samples[959] == 0.5f);
    CHECK(largestStep(rendered.samples) <= 0.5f / 960.0f * 1.001f);
}

TEST_CASE("Arrangements changed while stopped apply at once", "[engine]")
{
    const auto source = makeConstantSource(2, 96000, 1.0f, sampleRate);

    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(makeWholeFileArrangement(source, 1, 1.0f));
    (void) process(engine, 1);

    engine.setArrangement(makeWholeFileArrangement(source, 1, 0.5f));
    engine.play();
    const auto rendered = process(engine, 4);

    // Only the transport's fade-in, no gain ramp from the old setting.
    CHECK(rendered.samples[fadeLength - 1] == 0.5f);
    CHECK(rendered.samples[fadeLength] == 0.5f);
}

TEST_CASE("Rapid changes of arrangement wait for the crossfade in progress", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(constant(48000, 0.5f));
    engine.play();
    (void) process(engine, 4);

    std::vector<float> samples;
    std::size_t heapOperations = 0;
    for (int i = 0; i < 20; ++i)
    {
        engine.setArrangement(constant(48000, i % 2 == 0 ? -0.5f : 0.5f));
        const auto rendered = process(engine, 1);
        heapOperations += rendered.heapOperations;
        samples.insert(samples.end(), rendered.samples.begin(), rendered.samples.end());
        engine.collectGarbage();
    }

    CHECK(heapOperations == 0);
    CHECK(largestStep(samples) <= 1.0f / fadeLength * 1.001f);
}

TEST_CASE("The loop region comes with the arrangement", "[engine]")
{
    juce::AudioBuffer<float> ramp(1, 4000);
    for (int n = 0; n < 4000; ++n)
        ramp.setSample(0, n, static_cast<float>(n + 1));

    auto arrangement = makeWholeFileArrangement(daw::test::makeSource(ramp, sampleRate));
    arrangement->loopStart = 1000;
    arrangement->loopEnd = 1500;

    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(std::move(arrangement));
    engine.setLooping(true);
    engine.seek(1000);
    engine.play();

    const auto rendered = process(engine, 8);
    CHECK(rendered.samples[499] == 1500.0f);
    CHECK(rendered.samples[500] == 1001.0f); // wrapped
    CHECK(rendered.samples[1999] == 1500.0f); // the fourth time round
}

TEST_CASE("Replaced arrangements are freed on the message thread", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(constant(4800, 0.5f));
    (void) process(engine, 1);

    engine.setArrangement(constant(4800, 0.25f));
    const auto rendered = process(engine, 2); // swaps, retiring the first arrangement
    CHECK(rendered.heapOperations == 0);

    const auto before = daw::test::heapOperationsOnThisThread();
    engine.collectGarbage();
    CHECK(daw::test::heapOperationsOnThisThread() > before);
}

TEST_CASE("Arrangements set while the device is not running replace the previous one",
          "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);

    for (int i = 1; i <= 5; ++i)
        engine.setArrangement(constant(1000 * i, 0.1f));

    (void) process(engine, 1);
    CHECK(engine.getLength() == 5000);
}

TEST_CASE("Resetting the transport stops and goes back to 0", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(constant(48000, 0.5f));
    engine.seek(1000);
    engine.play();
    (void) process(engine, 4);

    engine.resetTransport();
    const auto rendered = process(engine, 2);

    CHECK(rendered.samples.front() > 0.0f); // fading out, no click
    CHECK(rendered.samples.back() == 0.0f);
    CHECK(engine.getTransportState() == TransportState::stopped);
    CHECK(engine.getPlayheadPosition() == 0);
}

TEST_CASE("Blocks larger than announced are rendered in pieces", "[engine]")
{
    juce::AudioBuffer<float> ramp(1, 10000);
    for (int n = 0; n < 10000; ++n)
        ramp.setSample(0, n, static_cast<float>(n + 1));

    Engine engine;
    engine.prepare(sampleRate, 64);
    engine.setArrangement(makeWholeFileArrangement(daw::test::makeSource(ramp, sampleRate)));
    engine.play();

    juce::AudioBuffer<float> block(2, 1000);
    std::size_t heapOperations = 0;
    for (int i = 0; i < 3; ++i)
        heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });

    CHECK(heapOperations == 0);
    CHECK(engine.getPlayheadPosition() == 3000);
    CHECK(block.getSample(0, 0) == 2001.0f);
    CHECK(block.getSample(1, 999) == 3000.0f);
}
