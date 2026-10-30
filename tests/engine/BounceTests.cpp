#include "daw/engine/Engine.h"
#include "daw/engine/OfflineRenderer.h"
#include "daw/engine/io/Bounce.h"

#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"
#include "support/TestAudioFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using daw::engine::Arrangement;
using daw::engine::ArrangementTrack;
using daw::engine::ClipPlacement;
using daw::engine::Engine;
using daw::engine::OfflineRenderer;
using daw::engine::io::BounceSettings;
using daw::engine::io::bounceToWav;
using daw::engine::io::quantise;
using daw::test::TemporaryDirectory;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int transportFade = 240; // 5 ms

std::shared_ptr<const daw::engine::MemoryAudioSource> makeNoise(int channels, int length,
                                                                int seed, float amplitude = 0.4f)
{
    juce::Random random(seed);
    juce::AudioBuffer<float> samples(channels, length);
    for (int channel = 0; channel < channels; ++channel)
        for (int n = 0; n < length; ++n)
            samples.setSample(channel, n, (random.nextFloat() * 2.0f - 1.0f) * amplitude);
    return daw::test::makeSource(std::move(samples), sampleRate);
}

// Several tracks, overlapping and trimmed clips, odd gains: a realistic mix.
std::unique_ptr<Arrangement> makeSong()
{
    auto drums = makeNoise(2, 60000, 1);
    auto bass = makeNoise(1, 40000, 2);
    auto pad = makeNoise(2, 90000, 3, 0.2f);

    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = sampleRate;

    ArrangementTrack one;
    one.id = 1;
    one.leftGain = 0.8f;
    one.rightGain = 1.1f;
    const std::vector<ClipPlacement> drumClips{
        {drums, 0, 60000, 0}, {drums, 30001, 50000, 1234}, {drums, 70000, 80000, 500}};
    one.segments = daw::engine::flattenClips(drumClips, 240);

    ArrangementTrack two;
    two.id = 2;
    two.leftGain = 1.3f;
    two.rightGain = 0.4f;
    const std::vector<ClipPlacement> bassClips{{bass, 12345, 52345, 0}};
    two.segments = daw::engine::flattenClips(bassClips, 240);

    ArrangementTrack three;
    three.id = 3;
    three.leftGain = 0.25f;
    three.rightGain = 0.25f;
    const std::vector<ClipPlacement> padClips{{pad, 5000, 95000, 0}};
    three.segments = daw::engine::flattenClips(padClips, 240);

    arrangement->tracks = {one, two, three};
    arrangement->masterGain = 0.9f;
    arrangement->length = 95000;
    arrangement->sources = {drums, bass, pad};
    return arrangement;
}

juce::AudioBuffer<float> renderOffline(const Arrangement& arrangement, juce::int64 start,
                                       int length)
{
    OfflineRenderer renderer(arrangement);
    renderer.setPosition(start);
    juce::AudioBuffer<float> output(2, length);
    renderer.render(output);
    return output;
}

// Plays the arrangement through the engine, as the audio device would, from start.
juce::AudioBuffer<float> playInRealTime(std::unique_ptr<Arrangement> arrangement,
                                        juce::int64 start, int length, int blockSize,
                                        std::size_t& heapOperations)
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(std::move(arrangement));
    engine.seek(start);
    engine.play();

    juce::AudioBuffer<float> output(2, length);
    juce::AudioBuffer<float> block(2, blockSize);
    for (int done = 0; done < length; done += blockSize)
    {
        heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
        const int copy = std::min(blockSize, length - done);
        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom(channel, done, block, channel, 0, copy);
    }

    return output;
}

std::unique_ptr<juce::AudioFormatReader> openWav(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    REQUIRE(reader != nullptr);
    return reader;
}

juce::AudioBuffer<float> readFloatFile(const juce::File& file, double& rate, int& bits)
{
    const auto reader = openWav(file);
    rate = reader->sampleRate;
    bits = static_cast<int>(reader->bitsPerSample);

    juce::AudioBuffer<float> samples(static_cast<int>(reader->numChannels),
                                     static_cast<int>(reader->lengthInSamples));
    REQUIRE(reader->read(&samples, 0, samples.getNumSamples(), 0, true, true));
    return samples;
}

std::vector<std::vector<int>> readIntegerFile(const juce::File& file, int bitsPerSample)
{
    const auto reader = openWav(file);
    REQUIRE(static_cast<int>(reader->bitsPerSample) == bitsPerSample);
    REQUIRE(!reader->usesFloatingPointData);

    const auto length = static_cast<int>(reader->lengthInSamples);
    std::vector<std::vector<int>> channels(2, std::vector<int>(static_cast<std::size_t>(length)));
    int* pointers[] = {channels[0].data(), channels[1].data()};
    REQUIRE(reader->read(pointers, 2, 0, length, false));

    // Left-justified 32-bit values back to the file's integer range.
    for (auto& channel : channels)
        for (auto& value : channel)
            value >>= 32 - bitsPerSample;
    return channels;
}

bool bitIdentical(const juce::AudioBuffer<float>& a, const juce::AudioBuffer<float>& b,
                  int from = 0)
{
    if (a.getNumSamples() != b.getNumSamples())
        return false;

    for (int channel = 0; channel < 2; ++channel)
        if (std::memcmp(a.getReadPointer(channel, from), b.getReadPointer(channel, from),
                        sizeof(float) * static_cast<std::size_t>(a.getNumSamples() - from)) != 0)
            return false;

    return true;
}

} // namespace

TEST_CASE("The export equals real-time playback sample for sample", "[engine][bounce]")
{
    const int deviceBlock = GENERATE(64, 256, 441, 1024);
    const juce::int64 start = GENERATE(juce::int64{0}, juce::int64{29999});
    const int length = 70000;

    std::size_t heapOperations = 0;
    const auto played = playInRealTime(makeSong(), start, length, deviceBlock, heapOperations);
    const auto song = makeSong();
    const auto exported = renderOffline(*song, start, length);

    CAPTURE(deviceBlock, start);
    CHECK(heapOperations == 0);

    // Playback starts with the transport's 5 ms fade-in; the export starts at full level.
    bool fadeInMatches = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < transportFade; ++i)
            fadeInMatches = fadeInMatches &&
                            played.getSample(channel, i) ==
                                exported.getSample(channel, i) *
                                    (static_cast<float>(i + 1) / static_cast<float>(transportFade));
    CHECK(fadeInMatches);

    // Everything after it is identical to the bit.
    CHECK(bitIdentical(played, exported, transportFade));
    CHECK(exported.getMagnitude(0, 0, length) > 0.1f);
}

TEST_CASE("Looped playback repeats the export of the loop exactly", "[engine][bounce]")
{
    auto arrangement = makeSong();
    arrangement->loopStart = 20000;
    arrangement->loopEnd = 30000;
    const auto exported = renderOffline(*arrangement, 20000, 10000);

    Engine engine;
    engine.prepare(sampleRate, 512);
    engine.setArrangement(std::move(arrangement));
    engine.setLooping(true);
    engine.seek(20000);
    engine.play();

    // Skip the first pass (it starts with the fade-in), then compare the next three.
    juce::AudioBuffer<float> block(2, 500);
    for (int i = 0; i < 20; ++i)
        engine.process(block);

    for (int pass = 0; pass < 3; ++pass)
    {
        juce::AudioBuffer<float> played(2, 10000);
        for (int done = 0; done < 10000; done += 500)
        {
            engine.process(block);
            for (int channel = 0; channel < 2; ++channel)
                played.copyFrom(channel, done, block, channel, 0, 500);
        }
        CHECK(bitIdentical(played, exported));
    }
}

TEST_CASE("A 32-bit float export holds the mix bit for bit", "[engine][bounce]")
{
    TemporaryDirectory directory;
    const auto song = makeSong();
    BounceSettings settings;
    settings.file = directory.file("mix.wav");
    settings.bitsPerSample = 32;
    settings.start = 1000;
    settings.end = 91000;

    const auto result = bounceToWav(*song, settings);
    REQUIRE(result.result.wasOk());
    CHECK_FALSE(result.cancelled);
    CHECK(result.samplesWritten == 90000);

    double rate = 0.0;
    int bits = 0;
    const auto written = readFloatFile(settings.file, rate, bits);
    CHECK(rate == sampleRate);
    CHECK(bits == 32);
    REQUIRE(written.getNumChannels() == 2);
    CHECK(bitIdentical(written, renderOffline(*song, 1000, 90000)));
    CHECK(result.peak ==
          std::max(written.getMagnitude(0, 0, 90000), written.getMagnitude(1, 0, 90000)));
}

TEST_CASE("16- and 24-bit exports hold the exactly quantised mix", "[engine][bounce]")
{
    const int bits = GENERATE(16, 24);
    TemporaryDirectory directory;
    const auto song = makeSong();
    BounceSettings settings;
    settings.file = directory.file("mix.wav");
    settings.bitsPerSample = bits;
    settings.end = 95000;

    REQUIRE(bounceToWav(*song, settings).result.wasOk());

    const auto written = readIntegerFile(settings.file, bits);
    const auto expected = renderOffline(*song, 0, 95000);

    CAPTURE(bits);
    bool exact = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int n = 0; n < 95000; ++n)
        {
            const auto& writtenChannel = written[static_cast<std::size_t>(channel)];
            const auto value = writtenChannel[static_cast<std::size_t>(n)];
            exact = exact && value == quantise(expected.getSample(channel, n), bits);
        }
    CHECK(exact);
}

TEST_CASE("Quantising rounds to the nearest step and clips at full scale", "[engine][bounce]")
{
    CHECK(quantise(0.0f, 16) == 0);
    CHECK(quantise(0.5f, 16) == 16384);
    CHECK(quantise(-0.5f, 16) == -16384);
    CHECK(quantise(1.0f, 16) == 32767);
    CHECK(quantise(-1.0f, 16) == -32768);
    CHECK(quantise(3.0f, 16) == 32767);
    CHECK(quantise(-3.0f, 24) == -8388608);
    CHECK(quantise(1.0f / 65536.0f, 16) == 0);       // half a step: to even
    CHECK(quantise(1.5f / 32768.0f, 16) == 2);        // 1.5 steps: to even
    CHECK(quantise(0.0f, 16, 0.75f) == 1);            // dither pushes it over
}

TEST_CASE("Dither is one LSB of triangular noise, the same on every export", "[engine][bounce]")
{
    TemporaryDirectory directory;
    const auto silence = std::make_unique<Arrangement>();
    silence->sampleRate = sampleRate;
    silence->length = 48000;

    const auto quiet = makeSong();

    BounceSettings settings;
    settings.bitsPerSample = 16;
    settings.dither = true;
    settings.end = 48000;

    settings.file = directory.file("a.wav");
    REQUIRE(bounceToWav(*quiet, settings).result.wasOk());
    settings.file = directory.file("b.wav");
    REQUIRE(bounceToWav(*quiet, settings).result.wasOk());
    CHECK(readIntegerFile(directory.file("a.wav"), 16) ==
          readIntegerFile(directory.file("b.wav"), 16));

    // On silence the dither alone: values -1, 0, 1, mean about 0, 0 most often.
    settings.file = directory.file("silence.wav");
    REQUIRE(bounceToWav(*silence, settings).result.wasOk());
    const auto values = readIntegerFile(settings.file, 16)[0];

    int counts[3] = {0, 0, 0};
    double sum = 0.0;
    for (const int value : values)
    {
        REQUIRE(value >= -1);
        REQUIRE(value <= 1);
        ++counts[value + 1];
        sum += value;
    }
    CHECK(std::abs(sum / static_cast<double>(values.size())) < 0.01);
    CHECK(counts[1] > counts[0]);
    CHECK(counts[1] > counts[2]);
    // Triangular density: P(0) = 3/4, P(±1) = 1/8 each.
    CHECK(std::abs(counts[1] / 48000.0 - 0.75) < 0.02);
}

TEST_CASE("A cancelled export leaves the existing file untouched", "[engine][bounce]")
{
    TemporaryDirectory directory;
    const auto file = directory.file("mix.wav");
    REQUIRE(file.replaceWithText("previous export"));

    const auto song = makeSong();
    BounceSettings settings;
    settings.file = file;
    settings.end = 95000;

    std::vector<double> progress;
    const auto result = bounceToWav(*song, settings, [&progress](double done) {
        progress.push_back(done);
        return done < 0.5;
    });

    CHECK(result.result.wasOk());
    CHECK(result.cancelled);
    CHECK(file.loadFileAsString() == "previous export");
    REQUIRE_FALSE(progress.empty());
    CHECK(progress.front() > 0.0);
    CHECK(std::is_sorted(progress.begin(), progress.end()));

    // Not cancelled: progress reaches 1 and the file is replaced.
    progress.clear();
    const auto finished = bounceToWav(*song, settings, [&progress](double done) {
        progress.push_back(done);
        return true;
    });
    CHECK(finished.result.wasOk());
    CHECK(progress.back() == 1.0);
    CHECK(file.loadFileAsString() != "previous export");
}

TEST_CASE("Exports that cannot be made fail with a reason", "[engine][bounce]")
{
    TemporaryDirectory directory;
    const auto song = makeSong();
    BounceSettings settings;
    settings.file = directory.file("mix.wav");
    settings.end = 1000;

    settings.bitsPerSample = 8;
    CHECK_THAT(bounceToWav(*song, settings).result.getErrorMessage().toStdString(),
               ContainsSubstring("16-bit, 24-bit and 32-bit float"));

    settings.bitsPerSample = 24;
    settings.end = 0;
    CHECK_THAT(bounceToWav(*song, settings).result.getErrorMessage().toStdString(),
               ContainsSubstring("nothing to export"));

    settings.end = 1000;
    settings.file = directory.file("missing/mix.wav");
    CHECK_THAT(bounceToWav(*song, settings).result.getErrorMessage().toStdString(),
               ContainsSubstring("folder does not exist"));
}
