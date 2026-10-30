#include "daw/engine/io/AudioFileLoader.h"

#include "support/SignalAnalysis.h"
#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

using daw::engine::io::loadAudioFile;
using daw::test::AudioFileType;
using daw::test::TemporaryDirectory;
using daw::test::writeAudioFile;
using Catch::Matchers::ContainsSubstring;

namespace
{

struct Encoding
{
    AudioFileType type;
    const char* extension;
    int bitsPerSample;
    bool floatingPoint;
    float tolerance; // largest expected difference after a write/read round trip
};

// A different tone on each channel, so that swapped channels would be noticed.
juce::AudioBuffer<float> makeTestSignal(int numChannels, int numSamples, double sampleRate)
{
    juce::AudioBuffer<float> buffer(numChannels, numSamples);
    for (int channel = 0; channel < numChannels; ++channel)
    {
        const auto tone = daw::test::makeSine(1, numSamples, 220.0 * (channel + 1), sampleRate, 0.8);
        buffer.copyFrom(channel, 0, tone, 0, 0, numSamples);
    }
    return buffer;
}

} // namespace

TEST_CASE("WAV and AIFF files load with their samples intact", "[engine][io]")
{
    const auto encoding = GENERATE(Encoding{AudioFileType::wav, ".wav", 16, false, 2.0f / 32768.0f},
                                   Encoding{AudioFileType::wav, ".wav", 24, false, 2.0f / 8388608.0f},
                                   Encoding{AudioFileType::wav, ".wav", 32, true, 0.0f},
                                   Encoding{AudioFileType::aiff, ".aiff", 16, false, 2.0f / 32768.0f},
                                   Encoding{AudioFileType::aiff, ".aiff", 24, false, 2.0f / 8388608.0f});
    const int numChannels = GENERATE(1, 2);
    const double sampleRate = GENERATE(44100.0, 48000.0, 96000.0);

    TemporaryDirectory directory;
    const auto file = directory.file(juce::String("test") + encoding.extension);
    const auto original = makeTestSignal(numChannels, 4800, sampleRate);
    REQUIRE(writeAudioFile(file, encoding.type, original, sampleRate, encoding.bitsPerSample,
                           encoding.floatingPoint));

    const auto loaded = loadAudioFile(file, sampleRate);

    CAPTURE(encoding.extension, encoding.bitsPerSample, numChannels, sampleRate, loaded.error);
    REQUIRE(loaded.succeeded());
    CHECK(loaded.error.isEmpty());
    CHECK(loaded.numChannels == numChannels);
    CHECK(loaded.sourceSampleRate == sampleRate);
    CHECK(loaded.sourceBitsPerSample == encoding.bitsPerSample);
    CHECK(loaded.audio->sampleRate == sampleRate);
    REQUIRE(loaded.audio->samples.getNumChannels() == numChannels);
    REQUIRE(loaded.audio->samples.getNumSamples() == original.getNumSamples());
    CHECK(daw::test::maxAbsDifference(loaded.audio->samples, original) <= encoding.tolerance);
}

TEST_CASE("A file at another sample rate is converted while loading", "[engine][io]")
{
    TemporaryDirectory directory;
    const auto file = directory.file("44k.wav");
    REQUIRE(writeAudioFile(file, AudioFileType::wav, makeTestSignal(2, 44100, 44100.0), 44100.0, 24));

    const auto loaded = loadAudioFile(file, 48000.0);

    REQUIRE(loaded.succeeded());
    CHECK(loaded.sourceSampleRate == 44100.0);
    CHECK(loaded.audio->sampleRate == 48000.0);
    CHECK(loaded.audio->samples.getNumChannels() == 2);
    CHECK(loaded.audio->samples.getNumSamples() == 48000);

    // Same tones, now sampled at 48 kHz (away from the edges).
    const auto expected = makeTestSignal(2, 48000, 48000.0);
    for (int channel = 0; channel < 2; ++channel)
    {
        const double errorDb = daw::test::toDecibels(
            daw::test::rmsDifference(loaded.audio->samples, channel, expected, channel, 2000, 44000) /
            daw::test::rms(expected, channel, 2000, 44000));
        CAPTURE(channel, errorDb);
        CHECK(errorDb < -90.0);
    }
}

TEST_CASE("Files that cannot be played are reported, not loaded", "[engine][io]")
{
    TemporaryDirectory directory;

    SECTION("missing file")
    {
        const auto loaded = loadAudioFile(directory.file("missing.wav"), 48000.0);
        CHECK_FALSE(loaded.succeeded());
        CHECK_THAT(loaded.error.toStdString(), ContainsSubstring("\"missing.wav\" does not exist"));
    }

    SECTION("empty file")
    {
        const auto file = directory.file("empty.wav");
        REQUIRE(file.create().wasOk());
        const auto loaded = loadAudioFile(file, 48000.0);
        CHECK_FALSE(loaded.succeeded());
        CHECK_THAT(loaded.error.toStdString(), ContainsSubstring("not a WAV or AIFF file"));
    }

    SECTION("text with an audio extension")
    {
        const auto file = directory.file("notes.aiff");
        REQUIRE(file.replaceWithText("This is not audio."));
        const auto loaded = loadAudioFile(file, 48000.0);
        CHECK_FALSE(loaded.succeeded());
        CHECK_THAT(loaded.error.toStdString(), ContainsSubstring("\"notes.aiff\""));
    }

    SECTION("supported by JUCE but not by this phase (FLAC)")
    {
        const auto file = directory.file("song.flac");
        REQUIRE(writeAudioFile(file, AudioFileType::flac, makeTestSignal(2, 4800, 48000.0), 48000.0, 16));
        const auto loaded = loadAudioFile(file, 48000.0);
        CHECK_FALSE(loaded.succeeded());
        CHECK_THAT(loaded.error.toStdString(), ContainsSubstring("not a WAV or AIFF file"));
    }

    SECTION("valid WAV without any samples")
    {
        const auto file = directory.file("silent.wav");
        REQUIRE(writeAudioFile(file, AudioFileType::wav, juce::AudioBuffer<float>(2, 0), 48000.0, 16));
        const auto loaded = loadAudioFile(file, 48000.0);
        CHECK_FALSE(loaded.succeeded());
        CHECK_THAT(loaded.error.toStdString(), ContainsSubstring("contains no audio"));
    }
}

TEST_CASE("Truncated files load what is there or fail cleanly", "[engine][io]")
{
    const auto type = GENERATE(AudioFileType::wav, AudioFileType::aiff);
    const auto keptFraction = GENERATE(0.5, 0.1, 0.01);

    TemporaryDirectory directory;
    const auto file = directory.file(type == AudioFileType::wav ? "cut.wav" : "cut.aiff");
    const int originalLength = 48000;
    REQUIRE(writeAudioFile(file, type, makeTestSignal(2, originalLength, 48000.0), 48000.0, 16));

    juce::MemoryBlock bytes;
    REQUIRE(file.loadFileAsData(bytes));
    bytes.setSize(static_cast<std::size_t>(static_cast<double>(bytes.getSize()) * keptFraction));
    REQUIRE(file.replaceWithData(bytes.getData(), bytes.getSize()));

    daw::engine::io::LoadedAudioFile loaded;
    REQUIRE_NOTHROW(loaded = loadAudioFile(file, 48000.0));

    CAPTURE(keptFraction, loaded.error);
    if (loaded.succeeded())
        CHECK(loaded.audio->samples.getNumSamples() <= originalLength);
    else
        CHECK(loaded.error.isNotEmpty());
}

TEST_CASE("Random bytes after a valid-looking header never crash the loader", "[engine][io]")
{
    TemporaryDirectory directory;
    juce::Random random(20261030); // fixed seed: the same inputs on every run

    int rejected = 0;
    for (int attempt = 0; attempt < 200; ++attempt)
    {
        const bool wav = attempt % 2 == 0;
        juce::MemoryOutputStream bytes;
        bytes.write(wav ? "RIFF" : "FORM", 4);
        bytes.writeInt(random.nextInt(1 << 20));
        bytes.write(wav ? "WAVEfmt " : "AIFFCOMM", 8);

        const int length = random.nextInt(512);
        for (int i = 0; i < length; ++i)
            bytes.writeByte(static_cast<char>(random.nextInt(256)));

        const auto file = directory.file(wav ? "fuzz.wav" : "fuzz.aiff");
        REQUIRE(file.replaceWithData(bytes.getData(), bytes.getDataSize()));

        daw::engine::io::LoadedAudioFile loaded;
        REQUIRE_NOTHROW(loaded = loadAudioFile(file, 48000.0));

        if (!loaded.succeeded())
        {
            ++rejected;
            CHECK(loaded.error.isNotEmpty());
        }
    }

    CHECK(rejected > 0);
}
