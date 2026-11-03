#include "daw/engine/io/WavRepair.h"

#include "daw/engine/io/RecordingWriter.h"
#include "support/TestArrangements.h"
#include "support/TestAudioFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <cstring>
#include <memory>

using daw::engine::io::repairWav;
using daw::test::TemporaryDirectory;
using daw::test::testInputSample;

namespace
{

constexpr double sampleRate = 48000.0;

void overwriteInt(const juce::File& file, juce::int64 position, juce::uint32 value)
{
    juce::FileOutputStream stream(file);
    REQUIRE(stream.setPosition(position));
    REQUIRE(stream.writeInt(static_cast<int>(value)));
}

juce::int64 dataChunkPosition(const juce::File& file)
{
    juce::MemoryBlock bytes;
    REQUIRE(file.loadFileAsData(bytes));
    for (std::size_t i = 12; i + 4 <= bytes.getSize(); ++i)
        if (std::memcmp(static_cast<const char*>(bytes.getData()) + i, "data", 4) == 0)
            return static_cast<juce::int64>(i);
    FAIL("no data chunk");
    return -1;
}

// What a crash leaves: a WAV with `flushed` + `more` frames on disk, never
// completed. Its header says `flushed` frames: the last update before the crash.
juce::File writeInterrupted(const juce::File& file, int numChannels, int flushed, int more)
{
    juce::String error;
    auto sink = daw::engine::io::openWavSink(file, numChannels, sampleRate, 24, error);
    REQUIRE(sink != nullptr);

    const auto block = [numChannels](int start, int length) {
        juce::AudioBuffer<float> audio(numChannels, length);
        for (int channel = 0; channel < numChannels; ++channel)
            for (int i = 0; i < length; ++i)
                audio.setSample(channel, i, testInputSample(channel, start + i));
        return audio;
    };

    REQUIRE(sink->write(block(0, flushed), flushed));
    REQUIRE(sink->flush());
    REQUIRE(sink->write(block(flushed, more), more));

    // Everything onto the disk (this also updates the header), then copy the file
    // before the sink completes it, and put the old lengths back into the copy.
    REQUIRE(sink->flush());
    const auto copy = file.getSiblingFile("crashed-" + file.getFileName());
    REQUIRE(file.copyFileTo(copy));
    sink.reset();

    const auto bytesPerFrame = static_cast<juce::uint32>(3 * numChannels);
    const auto staleData = bytesPerFrame * static_cast<juce::uint32>(flushed);
    const auto data = dataChunkPosition(copy);
    overwriteInt(copy, 4, static_cast<juce::uint32>(data) + staleData);
    overwriteInt(copy, data + 4, staleData);
    return copy;
}

bool holdsInput(const juce::File& file, int numChannels, juce::int64 numFrames)
{
    juce::WavAudioFormat wav;
    const std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(file.createInputStream().release(), true));
    if (reader == nullptr || reader->lengthInSamples != numFrames ||
        static_cast<int>(reader->numChannels) != numChannels)
        return false;

    juce::AudioBuffer<float> audio(numChannels, static_cast<int>(numFrames));
    reader->read(&audio, 0, static_cast<int>(numFrames), 0, true, true);
    for (int channel = 0; channel < numChannels; ++channel)
        for (int i = 0; i < static_cast<int>(numFrames); ++i)
            if (std::bit_cast<std::uint32_t>(audio.getSample(channel, i)) !=
                std::bit_cast<std::uint32_t>(testInputSample(channel, i) + 0.0f))
                return false;
    return true;
}

} // namespace

TEST_CASE("A WAV cut short by a crash is repaired to its whole frames", "[engine][recovery]")
{
    TemporaryDirectory directory;
    const auto crashed = writeInterrupted(directory.file("take.wav"), 2, 1000, 777);

    // The header says nothing was written (a crash before the first flush), and the
    // last frame is cut in the middle.
    const auto data = dataChunkPosition(crashed);
    overwriteInt(crashed, 4, 0);
    overwriteInt(crashed, data + 4, 0);
    {
        juce::FileOutputStream stream(crashed);
        REQUIRE(stream.setPosition(crashed.getSize() - 2));
        REQUIRE(stream.truncate().wasOk());
    }

    const auto result = repairWav(crashed);
    CAPTURE(result.result.getErrorMessage());
    REQUIRE(result.result.wasOk());
    CHECK(result.changed);
    CHECK(result.numFrames == 1776); // 1777 frames, the last one incomplete
    CHECK(result.sampleRate == sampleRate);
    CHECK(result.numChannels == 2);
    CHECK(holdsInput(crashed, 2, 1776));

    // Repairing again changes nothing.
    const auto again = repairWav(crashed);
    CHECK(again.result.wasOk());
    CHECK_FALSE(again.changed);
    CHECK(again.numFrames == 1776);
}

TEST_CASE("An odd-sized data chunk gets its pad byte", "[engine][recovery]")
{
    TemporaryDirectory directory;
    const auto crashed = writeInterrupted(directory.file("mono.wav"), 1, 10, 5); // 45 bytes
    overwriteInt(crashed, dataChunkPosition(crashed) + 4, 0);

    const auto result = repairWav(crashed);
    REQUIRE(result.result.wasOk());
    CHECK(result.numFrames == 15);
    CHECK(crashed.getSize() == dataChunkPosition(crashed) + 8 + 46);
    CHECK(holdsInput(crashed, 1, 15));
}

TEST_CASE("Long recordings become RF64, and RF64 headers are repaired", "[engine][recovery]")
{
    TemporaryDirectory directory;
    const auto crashed = writeInterrupted(directory.file("long.wav"), 2, 2000, 500);

    // As if 2500 frames (15,000 bytes) were beyond what a RIFF header can describe.
    // (JUCE's reader only takes RF64 files that really are that long, so the header is
    // checked here directly.)
    const auto converted = repairWav(crashed, 10000);
    REQUIRE(converted.result.wasOk());
    CHECK(converted.changed);
    CHECK(converted.numFrames == 2500);
    {
        juce::FileInputStream stream(crashed);
        CHECK(stream.readInt() == static_cast<int>(juce::ByteOrder::littleEndianInt("RF64")));
        CHECK(stream.readInt() == -1);
        CHECK(stream.readInt() == static_cast<int>(juce::ByteOrder::littleEndianInt("WAVE")));
        CHECK(stream.readInt() == static_cast<int>(juce::ByteOrder::littleEndianInt("ds64")));
        stream.readInt();
        CHECK(stream.readInt64() == crashed.getSize() - 8);  // the RIFF size
        CHECK(stream.readInt64() == 15000);                  // the data size
        CHECK(stream.readInt64() == 2500);                   // frames
        REQUIRE(stream.setPosition(dataChunkPosition(crashed) + 4));
        CHECK(stream.readInt() == -1);
    }

    // More audio reached the disk after the last header update.
    {
        juce::FileOutputStream stream(crashed);
        juce::MemoryBlock frames(6 * 100, true);
        REQUIRE(stream.write(frames.getData(), frames.getSize()));
    }
    const auto grown = repairWav(crashed, 10000);
    REQUIRE(grown.result.wasOk());
    CHECK(grown.changed);
    CHECK(grown.numFrames == 2600);

    // Below the limit again, it goes back to a plain RIFF file that reads back.
    const auto riff = repairWav(crashed);
    REQUIRE(riff.result.wasOk());
    CHECK(riff.changed);
    CHECK(crashed.loadFileAsString().startsWith("RIFF"));
    juce::WavAudioFormat wav;
    const std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(crashed.createInputStream().release(), true));
    REQUIRE(reader != nullptr);
    CHECK(reader->lengthInSamples == 2600);
}

TEST_CASE("Files that are not WAV recordings are refused", "[engine][recovery]")
{
    TemporaryDirectory directory;
    const auto text = directory.file("notes.wav");
    REQUIRE(text.replaceWithText("not audio at all"));

    CHECK(repairWav(text).result.failed());
    CHECK(repairWav(directory.file("missing.wav")).result.failed());
}
