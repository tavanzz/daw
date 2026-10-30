#include "daw/engine/Engine.h"

#include "support/RealtimeCheck.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>
#include <cmath>
#include <memory>
#include <vector>

using daw::engine::AudioData;
using daw::engine::Engine;
using daw::engine::TransportState;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;

std::unique_ptr<AudioData> makeAudio(int length, float value, double rate = sampleRate)
{
    auto audio = std::make_unique<AudioData>();
    audio->sampleRate = rate;
    audio->samples.setSize(2, length);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(audio->samples.getWritePointer(channel), value, length);
    return audio;
}

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

TEST_CASE("Engine plays loaded audio once told to, from the next block", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setAudio(makeAudio(48000, 0.5f));

    (void) process(engine, 1);
    CHECK(engine.getAudioLength() == 48000);
    CHECK(engine.getTransportState() == TransportState::stopped);

    engine.play();
    CHECK(engine.getTransportState() == TransportState::stopped); // not processed yet

    const auto rendered = process(engine, 10);
    CHECK(rendered.heapOperations == 0);
    CHECK(engine.getTransportState() == TransportState::playing);
    CHECK(engine.getPlayheadPosition() == 10 * blockSize);
    CHECK(rendered.samples.back() == 0.5f);
}

TEST_CASE("Engine publishes pause, stop, seek and loop", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setAudio(makeAudio(48000, 0.5f));
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

    engine.stop();
    (void) process(engine, 1);
    CHECK(engine.getTransportState() == TransportState::stopped);
    CHECK(engine.getPlayheadPosition() == 0);
}

TEST_CASE("Engine plays silence for audio at another sample rate", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setAudio(makeAudio(48000, 0.5f, 44100.0));
    engine.play();

    const auto rendered = process(engine, 10);
    CHECK(std::all_of(rendered.samples.begin(), rendered.samples.end(),
                      [](float value) { return value == 0.0f; }));
}

TEST_CASE("Replacing the audio fades the old audio out before switching", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setAudio(makeAudio(48000, 0.5f));
    engine.play();
    (void) process(engine, 10);

    // A new file, played straight away: the play command must reach the new audio.
    engine.setAudio(makeAudio(24000, -0.25f));
    engine.play();
    const auto rendered = process(engine, 20);

    float largestStep = 0.0f;
    for (std::size_t i = 1; i < rendered.samples.size(); ++i)
        largestStep = std::max(largestStep, std::abs(rendered.samples[i] - rendered.samples[i - 1]));

    CHECK(rendered.heapOperations == 0);
    CHECK(rendered.samples.front() > 0.0f); // still the old audio, fading out
    CHECK(rendered.samples.back() == -0.25f);
    CHECK(largestStep <= 0.5f / 240.0f * 1.001f);
    CHECK(engine.getAudioLength() == 24000);
    CHECK(engine.getTransportState() == TransportState::playing);
}

TEST_CASE("Replaced audio is freed on the message thread, not the audio thread", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setAudio(makeAudio(4800, 0.5f));
    (void) process(engine, 1);

    engine.setAudio(makeAudio(4800, 0.25f));
    const auto rendered = process(engine, 2); // swaps, retiring the first audio
    CHECK(rendered.heapOperations == 0);

    const auto before = daw::test::heapOperationsOnThisThread();
    engine.collectGarbage();
    CHECK(daw::test::heapOperationsOnThisThread() > before);
}

TEST_CASE("Audio set while the device is not running replaces the previous one", "[engine]")
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);

    for (int i = 1; i <= 5; ++i)
        engine.setAudio(makeAudio(1000 * i, 0.1f));

    (void) process(engine, 1);
    CHECK(engine.getAudioLength() == 5000);
}
