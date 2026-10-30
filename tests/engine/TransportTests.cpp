#include "daw/engine/Transport.h"

#include "support/RealtimeCheck.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <cmath>
#include <vector>

using daw::engine::Transport;
using daw::engine::TransportState;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int fadeLength = 240; // 5 ms at 48 kHz

// Sample n of channel 0 holds n + 1, so every output value names the source sample
// it came from (and 0 means silence). Channel 1 holds the negated values.
juce::AudioBuffer<float> makeRamp(int numChannels, int length)
{
    juce::AudioBuffer<float> buffer(numChannels, length);
    for (int channel = 0; channel < numChannels; ++channel)
        for (int n = 0; n < length; ++n)
            buffer.setSample(channel, n, (channel == 0 ? 1.0f : -1.0f) * static_cast<float>(n + 1));
    return buffer;
}

juce::AudioBuffer<float> makeConstant(int length, float value)
{
    juce::AudioBuffer<float> buffer(1, length);
    juce::FloatVectorOperations::fill(buffer.getWritePointer(0), value, length);
    return buffer;
}

Transport makeTransport(const juce::AudioBuffer<float>& source)
{
    Transport transport;
    transport.prepare(sampleRate);
    transport.setSourceLength(source.getNumSamples());
    return transport;
}

// Renders numSamples in blocks of blockSize; returns output channel 0.
std::vector<float> render(Transport& transport, const juce::AudioBuffer<float>& source,
                          int numSamples, int blockSize = 64)
{
    std::vector<float> rendered;
    juce::AudioBuffer<float> block(2, blockSize);

    for (int done = 0; done < numSamples; done += blockSize)
    {
        const int length = std::min(blockSize, numSamples - done);
        transport.render(source, block, 0, length);
        rendered.insert(rendered.end(), block.getReadPointer(0), block.getReadPointer(0) + length);
    }

    return rendered;
}

// What the transport outputs for source sample `index` at fade level `level`.
float faded(int index, int level)
{
    return static_cast<float>(index + 1) * (static_cast<float>(level) / static_cast<float>(fadeLength));
}

float largestStep(const std::vector<float>& samples)
{
    float largest = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    return largest;
}

bool allSilent(const std::vector<float>& samples, std::size_t from, std::size_t to)
{
    return std::all_of(samples.begin() + static_cast<std::ptrdiff_t>(from),
                       samples.begin() + static_cast<std::ptrdiff_t>(to),
                       [](float value) { return value == 0.0f; });
}

} // namespace

TEST_CASE("The fade lasts 5 ms at the prepared sample rate", "[engine][transport]")
{
    Transport transport;

    transport.prepare(48000.0);
    CHECK(transport.getFadeLength() == 240);
    transport.prepare(44100.0);
    CHECK(transport.getFadeLength() == 221); // 220.5 rounds up
    transport.prepare(96000.0);
    CHECK(transport.getFadeLength() == 480);
}

TEST_CASE("A new source is stopped at the start and renders silence", "[engine][transport]")
{
    const auto source = makeRamp(2, 1000);
    auto transport = makeTransport(source);

    CHECK(transport.getState() == TransportState::stopped);
    CHECK(transport.getPosition() == 0);
    CHECK_FALSE(transport.isRunning());

    const auto output = render(transport, source, 500);
    CHECK(allSilent(output, 0, output.size()));
    CHECK(transport.getPosition() == 0);
}

TEST_CASE("Playback fades in, then plays the source exactly", "[engine][transport]")
{
    const auto source = makeRamp(2, 5000);
    auto transport = makeTransport(source);

    transport.play();
    CHECK(transport.getState() == TransportState::playing);
    const auto output = render(transport, source, 2000);

    bool fadeInExact = true;
    for (int i = 0; i < fadeLength; ++i)
        fadeInExact = fadeInExact && output[static_cast<std::size_t>(i)] == faded(i, i + 1);

    bool restExact = true;
    for (int i = fadeLength; i < 2000; ++i)
        restExact = restExact && output[static_cast<std::size_t>(i)] == static_cast<float>(i + 1);

    CHECK(fadeInExact);
    CHECK(restExact);
    CHECK(transport.getPosition() == 2000);
}

TEST_CASE("The position advances by exactly the rendered samples, whatever the block size",
          "[engine][transport]")
{
    const auto source = makeRamp(2, 3000);

    auto reference = makeTransport(source);
    reference.setLooping(true);
    reference.play();
    const auto expected = render(reference, source, 10000, 64);

    const int blockSize = GENERATE(1, 7, 333, 512, 4096);
    auto transport = makeTransport(source);
    transport.setLooping(true);
    transport.play();
    const auto output = render(transport, source, 10000, blockSize);

    CAPTURE(blockSize);
    CHECK(output == expected);
    CHECK(transport.getPosition() == 10000 % 3000);
}

TEST_CASE("Looping wraps to the start with no gap and no fade", "[engine][transport]")
{
    const auto source = makeRamp(1, 1000);
    auto transport = makeTransport(source);
    transport.setLooping(true);
    transport.play();

    const auto output = render(transport, source, 3500, 333);

    bool continuous = true;
    for (int i = fadeLength; i < 3500; ++i)
        continuous = continuous && output[static_cast<std::size_t>(i)] == static_cast<float>(i % 1000 + 1);

    CHECK(continuous);
    CHECK(transport.getState() == TransportState::playing);
    CHECK(transport.getPosition() == 500);
}

TEST_CASE("A loop range repeats only that range", "[engine][transport]")
{
    const auto source = makeRamp(1, 1000);
    auto transport = makeTransport(source);
    transport.setLoopRange(300, 400);
    transport.setLooping(true);
    transport.play();

    CHECK(transport.getLoopStart() == 300);
    CHECK(transport.getLoopEnd() == 400);

    const auto output = render(transport, source, 1000, 128);

    bool followsRange = true;
    for (int i = fadeLength; i < 1000; ++i)
    {
        const int expectedIndex = i < 400 ? i : 300 + (i - 400) % 100;
        followsRange = followsRange && output[static_cast<std::size_t>(i)] == static_cast<float>(expectedIndex + 1);
    }

    CHECK(followsRange);
}

TEST_CASE("Playback after the loop range plays on to the end", "[engine][transport]")
{
    const auto source = makeRamp(1, 1000);
    auto transport = makeTransport(source);
    transport.setLoopRange(300, 400);
    transport.setLooping(true);
    transport.seek(500);
    transport.play();

    const auto output = render(transport, source, 1000, 100);

    CHECK(output[499] == 1000.0f); // source sample 999, the last one
    CHECK(allSilent(output, 500, 1000));
    CHECK(transport.getState() == TransportState::stopped);
}

TEST_CASE("An invalid loop range means the whole source", "[engine][transport]")
{
    const auto source = makeRamp(1, 1000);
    auto transport = makeTransport(source);

    transport.setLoopRange(600, 600);
    CHECK(transport.getLoopStart() == 0);
    CHECK(transport.getLoopEnd() == 1000);

    transport.setLoopRange(800, 200);
    CHECK(transport.getLoopStart() == 0);
    CHECK(transport.getLoopEnd() == 1000);
}

TEST_CASE("Without looping, playback stops at the end and returns to the start", "[engine][transport]")
{
    const auto source = makeRamp(1, 1000);
    auto transport = makeTransport(source);
    transport.play();

    const auto output = render(transport, source, 1500, 1500);

    CHECK(output[999] == 1000.0f);
    CHECK(allSilent(output, 1000, 1500));
    CHECK(transport.getState() == TransportState::stopped);
    CHECK(transport.getPosition() == 0);
    CHECK_FALSE(transport.isRunning());
}

TEST_CASE("Pause fades out and resume continues from the very next sample", "[engine][transport]")
{
    const auto source = makeRamp(1, 10000);
    auto transport = makeTransport(source);
    transport.play();
    (void) render(transport, source, 1000, 100);

    transport.pause();
    CHECK(transport.getState() == TransportState::paused); // reported immediately
    const auto pausing = render(transport, source, 1000, 100);

    bool fadeOutExact = true;
    for (int i = 0; i < fadeLength; ++i)
        fadeOutExact = fadeOutExact && pausing[static_cast<std::size_t>(i)] == faded(1000 + i, fadeLength - (i + 1));

    CHECK(fadeOutExact);
    CHECK(allSilent(pausing, fadeLength, pausing.size()));
    CHECK(transport.getPosition() == 1000 + fadeLength);

    transport.play();
    const auto resumed = render(transport, source, 1000, 100);

    // Nothing skipped and nothing repeated: the first resumed sample is the one after
    // the last sample of the fade-out.
    CHECK(resumed[0] == faded(1000 + fadeLength, 1));
    CHECK(resumed[fadeLength] == static_cast<float>(1000 + 2 * fadeLength + 1));
    CHECK(transport.getPosition() == 1000 + fadeLength + 1000);
}

TEST_CASE("Stop fades out and returns to the start; play then starts from 0", "[engine][transport]")
{
    const auto source = makeRamp(1, 10000);
    auto transport = makeTransport(source);
    transport.play();
    (void) render(transport, source, 3000);

    transport.stop();
    CHECK(transport.getState() == TransportState::stopped);
    const auto stopping = render(transport, source, 500);

    CHECK(stopping[0] == faded(3000, fadeLength - 1));
    CHECK(allSilent(stopping, fadeLength, stopping.size()));
    CHECK(transport.getPosition() == 0);

    transport.play();
    const auto restarted = render(transport, source, 500);
    CHECK(restarted[0] == faded(0, 1));
    CHECK(restarted[fadeLength] == static_cast<float>(fadeLength + 1));
}

TEST_CASE("Stop while paused returns to the start at once", "[engine][transport]")
{
    const auto source = makeRamp(1, 10000);
    auto transport = makeTransport(source);
    transport.play();
    (void) render(transport, source, 2000);
    transport.pause();
    (void) render(transport, source, 500);
    REQUIRE(transport.getPosition() == 2000 + fadeLength);

    transport.stop();
    CHECK(transport.getState() == TransportState::stopped);
    CHECK(transport.getPosition() == 0);
}

TEST_CASE("Play during the stop fade-out restarts from the beginning", "[engine][transport]")
{
    const auto source = makeRamp(1, 10000);
    auto transport = makeTransport(source);
    transport.play();
    (void) render(transport, source, 1000, 100);

    transport.stop();
    (void) render(transport, source, 100, 100); // fade-out only partly done
    transport.play();
    CHECK(transport.getState() == TransportState::playing);

    // The remaining 140 fade-out samples, then the start of the source again.
    const auto output = render(transport, source, 1000, 100);
    const int remainingFade = fadeLength - 100;

    CHECK(output[static_cast<std::size_t>(remainingFade - 1)] == 0.0f);
    CHECK(output[static_cast<std::size_t>(remainingFade)] == faded(0, 1));
    CHECK(transport.getPosition() == 1000 - remainingFade);
}

TEST_CASE("Seeking while stopped or paused moves the position at once", "[engine][transport]")
{
    const auto source = makeRamp(1, 10000);
    auto transport = makeTransport(source);

    transport.seek(4000);
    CHECK(transport.getPosition() == 4000);
    CHECK(transport.getState() == TransportState::stopped);

    transport.play();
    const auto output = render(transport, source, 500);
    CHECK(output[0] == faded(4000, 1));
    CHECK(output[fadeLength] == static_cast<float>(4000 + fadeLength + 1));
}

TEST_CASE("Seeking while playing fades out, jumps and fades back in", "[engine][transport]")
{
    const auto source = makeRamp(1, 10000);
    auto transport = makeTransport(source);
    transport.play();
    (void) render(transport, source, 1000, 100);

    transport.seek(5000);
    CHECK(transport.getState() == TransportState::playing);
    const auto output = render(transport, source, 1000, 100);

    CHECK(output[0] == faded(1000, fadeLength - 1));
    CHECK(output[fadeLength - 1] == 0.0f);
    CHECK(output[fadeLength] == faded(5000, 1));
    CHECK(output[2 * fadeLength] == static_cast<float>(5000 + fadeLength + 1));
    CHECK(transport.getPosition() == 5000 + (1000 - fadeLength));
}

TEST_CASE("Seek positions are clamped to the source", "[engine][transport]")
{
    const auto source = makeRamp(1, 1000);
    auto transport = makeTransport(source);

    transport.seek(-50);
    CHECK(transport.getPosition() == 0);
    transport.seek(999999);
    CHECK(transport.getPosition() == 1000);
}

TEST_CASE("No transport action makes the output jump by more than one fade step",
          "[engine][transport]")
{
    // A full-scale constant makes any discontinuity visible as a step in the output.
    const auto source = makeConstant(100000, 1.0f);
    auto transport = makeTransport(source);
    std::vector<float> output;
    const auto append = [&](int numSamples) {
        const auto block = render(transport, source, numSamples, 37);
        output.insert(output.end(), block.begin(), block.end());
    };

    append(100);
    transport.play();
    append(1000);
    transport.pause();
    append(100); // reverse in the middle of the fade-out
    transport.play();
    append(1000);
    transport.seek(50000);
    append(1000);
    transport.pause();
    append(1000);
    transport.play();
    append(1000);
    transport.stop();
    append(1000);

    const float oneStep = 1.0f / static_cast<float>(fadeLength);
    CHECK(largestStep(output) <= oneStep * 1.0001f);
    CHECK(output.back() == 0.0f);
}

TEST_CASE("Mono sources feed both outputs; extra outputs stay silent", "[engine][transport]")
{
    const auto source = GENERATE(1, 2);
    const auto audio = makeRamp(source, 2000);
    auto transport = makeTransport(audio);
    transport.play();

    juce::AudioBuffer<float> output(4, 1000);
    output.clear();
    juce::FloatVectorOperations::fill(output.getWritePointer(2), 9.0f, 1000);
    transport.render(audio, output, 0, 1000);

    const float left = output.getSample(0, 500);
    const float right = output.getSample(1, 500);

    CAPTURE(source);
    CHECK(left == 501.0f);
    CHECK(right == (source == 1 ? 501.0f : -501.0f));
    CHECK(output.getMagnitude(2, 0, 1000) == 0.0f);
    CHECK(output.getMagnitude(3, 0, 1000) == 0.0f);
}

TEST_CASE("Play without a source does nothing", "[engine][transport]")
{
    Transport transport;
    transport.prepare(sampleRate);
    transport.setSourceLength(0);

    transport.play();
    CHECK(transport.getState() == TransportState::stopped);
    CHECK_FALSE(transport.isRunning());
}

TEST_CASE("Rendering and every command are real-time safe", "[engine][transport]")
{
    const auto source = makeRamp(2, 20000);
    auto transport = makeTransport(source);
    juce::AudioBuffer<float> block(2, 512);

    const auto heapOperations = daw::test::heapOperationsDuring([&] {
        transport.setLooping(true);
        transport.play();
        for (int i = 0; i < 20; ++i)
            transport.render(source, block, 0, 512);
        transport.seek(100);
        transport.render(source, block, 0, 512);
        transport.pause();
        transport.render(source, block, 0, 512);
        transport.stop();
        transport.render(source, block, 0, 512);
    });

    CHECK(heapOperations == 0);
}
