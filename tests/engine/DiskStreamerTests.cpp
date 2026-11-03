#include "daw/engine/io/DiskStreamer.h"

#include "daw/engine/Mixer.h"
#include "daw/engine/io/AudioFileLoader.h"
#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"
#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <bit>
#include <cstdint>
#include <memory>
#include <vector>

using daw::engine::Arrangement;
using daw::engine::AudioSource;
using daw::engine::io::DiskStreamer;
using daw::engine::io::StreamingAudioSource;
using daw::test::TemporaryDirectory;
using daw::test::testInputSample;

namespace
{

constexpr double streamRate = 48000.0;
constexpr int blockLength = DiskStreamer::blockLength;

juce::File writeTestFile(const TemporaryDirectory& directory, const juce::String& name,
                         int numChannels, int length, double rate)
{
    juce::AudioBuffer<float> audio(numChannels, length);
    for (int channel = 0; channel < numChannels; ++channel)
        for (int i = 0; i < length; ++i)
            audio.setSample(channel, i, testInputSample(channel, i) * 0.9f);

    const auto file = directory.file(name);
    REQUIRE(daw::test::writeAudioFile(file, daw::test::AudioFileType::wav, audio, rate, 32, true));
    return file;
}

std::shared_ptr<const AudioSource> inMemory(const juce::File& file)
{
    auto loaded = daw::engine::io::loadAudioFile(file, streamRate);
    REQUIRE(loaded.succeeded());
    return std::make_shared<const daw::engine::MemoryAudioSource>(std::move(*loaded.audio));
}

std::vector<float> readRange(const AudioSource& source, int channel, juce::int64 start, int length,
                             bool* complete = nullptr)
{
    std::vector<float> samples(static_cast<std::size_t>(length));
    const bool whole = source.read(channel, start, samples.data(), length);
    if (complete != nullptr)
        *complete = whole;
    return samples;
}

bool sameBits(const std::vector<float>& a, const std::vector<float>& b)
{
    if (a.size() != b.size())
        return false;
    for (std::size_t i = 0; i < a.size(); ++i)
        if (std::bit_cast<std::uint32_t>(a[i]) != std::bit_cast<std::uint32_t>(b[i]))
            return false;
    return true;
}

std::unique_ptr<Arrangement> wholeFile(const std::shared_ptr<const AudioSource>& source)
{
    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = streamRate;
    arrangement->length = source->getLength();
    daw::engine::ArrangementTrack track;
    track.id = 1;
    track.segments.push_back({source.get(), 0, source->getLength(), 0, 0, 0});
    arrangement->tracks.push_back(track);
    arrangement->sources.push_back(source);
    return arrangement;
}

DiskStreamer::Settings settingsFor(DiskStreamer::Mode mode)
{
    DiskStreamer::Settings settings;
    settings.sampleRate = streamRate;
    settings.mode = mode;
    return settings;
}

// A reader that delivers nothing until the test opens its gate: a stalled disk.
class StalledReader final : public juce::AudioFormatReader
{
public:
    StalledReader(std::unique_ptr<juce::AudioFormatReader> readerToWrap,
                  juce::WaitableEvent& gateToWait)
        : juce::AudioFormatReader(nullptr, readerToWrap->getFormatName())
        , inner(std::move(readerToWrap))
        , gate(gateToWait)
    {
        sampleRate = inner->sampleRate;
        bitsPerSample = inner->bitsPerSample;
        lengthInSamples = inner->lengthInSamples;
        numChannels = inner->numChannels;
        usesFloatingPointData = inner->usesFloatingPointData;
    }

    bool readSamples(int* const* destChannels, int numDestChannels, int startOffsetInDestBuffer,
                     juce::int64 startSampleInFile, int numSamples) override
    {
        gate.wait(-1);
        return inner->readSamples(destChannels, numDestChannels, startOffsetInDestBuffer,
                                  startSampleInFile, numSamples);
    }

private:
    std::unique_ptr<juce::AudioFormatReader> inner;
    juce::WaitableEvent& gate;
};

std::unique_ptr<juce::AudioFormatReader> wavReader(const juce::File& file)
{
    juce::WavAudioFormat wav;
    return std::unique_ptr<juce::AudioFormatReader>(
        wav.createReaderFor(file.createInputStream().release(), true));
}

} // namespace

TEST_CASE("Streamed audio equals the whole file in memory, also converted, to the bit",
          "[engine][streaming]")
{
    const double fileRate = GENERATE(48000.0, 44100.0, 96000.0);
    const int numChannels = GENERATE(1, 2);
    CAPTURE(fileRate, numChannels);

    TemporaryDirectory directory;
    const auto file = writeTestFile(directory, "file.wav", numChannels,
                                    static_cast<int>(2.6 * blockLength * fileRate / streamRate),
                                    fileRate);
    const auto memory = inMemory(file);

    for (const auto mode : {DiskStreamer::Mode::manual, DiskStreamer::Mode::synchronous})
    {
        DiskStreamer streamer(settingsFor(mode), nullptr);
        juce::String error;
        const auto streamed = streamer.open(file, error);
        REQUIRE(streamed != nullptr);
        CHECK(streamed->getLength() == memory->getLength());
        CHECK(streamed->getNumChannels() == numChannels);
        CHECK(streamed->getSampleRate() == streamRate);

        if (mode == DiskStreamer::Mode::manual)
        {
            streamer.setPlan(*wholeFile(streamed));
            streamer.prefetch(0, streamed->getLength());
        }

        const auto length = static_cast<int>(memory->getLength());
        const std::pair<juce::int64, int> ranges[] = {
            {0, length}, {-100, 300}, {blockLength - 7, 20}, {2 * blockLength - 1, 2},
            {length - 50, 200}, {12345, blockLength + 999}};

        for (int channel = 0; channel < numChannels; ++channel)
            for (const auto& [start, count] : ranges)
            {
                CAPTURE(channel, start, count);
                bool complete = false;
                CHECK(sameBits(readRange(*streamed, channel, start, count, &complete),
                               readRange(*memory, channel, start, count)));
                CHECK(complete);
            }

        CHECK(streamer.getDropouts() == 0);
    }
}

TEST_CASE("The read-ahead follows the playhead through loops and seeks", "[engine][streaming]")
{
    TemporaryDirectory directory;
    const auto file = writeTestFile(directory, "long.wav", 2, 10 * blockLength, streamRate);
    const auto memory = inMemory(file);

    auto settings = settingsFor(DiskStreamer::Mode::manual);
    settings.lookaheadSeconds = 1.0;
    settings.memoryBudget = 8 * 2 * blockLength * sizeof(float); // eight stereo blocks
    daw::engine::RenderClock clock;
    DiskStreamer streamer(settings, &clock);

    juce::String error;
    const auto streamed = streamer.open(file, error);
    REQUIRE(streamed != nullptr);
    const auto streamedArrangement = wholeFile(streamed);
    const auto memoryArrangement = wholeFile(memory);
    streamer.setPlan(*streamedArrangement);

    DiskStreamer::Playhead playhead;
    playhead.playing = true;
    streamer.followPlayhead([&playhead] { return playhead; });

    daw::engine::Mixer streamedMixer;
    daw::engine::Mixer memoryMixer;
    streamedMixer.prepare(streamRate, 512);
    memoryMixer.prepare(streamRate, 512);
    auto streamedState = daw::engine::MixState::startingAt(*streamedArrangement);
    auto memoryState = daw::engine::MixState::startingAt(*memoryArrangement);
    juce::AudioBuffer<float> fromDisk(2, 512);
    juce::AudioBuffer<float> fromMemory(2, 512);

    // Plays on, seeks (with a prefetch, as the transport waits for one), loops.
    std::size_t heapOperations = 0;
    bool identical = true;
    const auto playBlocks = [&](int numBlocks) {
        for (int i = 0; i < numBlocks; ++i)
        {
            if (playhead.looping && playhead.position >= playhead.loopEnd)
                playhead.position = playhead.loopStart;
            streamer.service();

            clock.begin();
            heapOperations += daw::test::heapOperationsDuring([&] {
                streamedMixer.render(*streamedArrangement, streamedState, playhead.position,
                                     fromDisk, 0, 512);
            });
            clock.end();
            memoryMixer.render(*memoryArrangement, memoryState, playhead.position, fromMemory, 0,
                               512);

            const auto samples = [](const juce::AudioBuffer<float>& buffer, int channel) {
                return std::vector<float>(buffer.getReadPointer(channel),
                                          buffer.getReadPointer(channel) + 512);
            };
            for (int channel = 0; channel < 2; ++channel)
                identical = identical && sameBits(samples(fromDisk, channel),
                                                  samples(fromMemory, channel));
            playhead.position += 512;
        }
    };

    playBlocks(200);
    playhead.position = 7 * blockLength + 1000;
    streamer.prefetch(playhead.position, 48000);
    playBlocks(100);
    playhead.looping = true;
    playhead.loopStart = 2 * blockLength - 3000;
    playhead.loopEnd = 2 * blockLength + 9000; // across a block boundary, wrapping often
    playhead.position = playhead.loopStart;
    streamer.prefetch(playhead.position, 48000);
    playBlocks(300);

    CHECK(identical);
    CHECK(heapOperations == 0);
    CHECK(streamer.getDropouts() == 0);
    CHECK(streamer.getMemoryInUse() <= settings.memoryBudget);
}

TEST_CASE("A stalled disk gives counted silence and never makes the reader wait",
          "[engine][streaming]")
{
    TemporaryDirectory directory;
    const auto file = writeTestFile(directory, "stalled.wav", 1, 3 * blockLength, streamRate);
    const auto memory = inMemory(file);

    juce::WaitableEvent gate(true);
    auto settings = settingsFor(DiskStreamer::Mode::readAhead);
    settings.openReader = [&gate](const juce::File& toOpen) {
        return std::make_unique<StalledReader>(wavReader(toOpen), gate);
    };

    std::shared_ptr<StreamingAudioSource> streamed;
    {
        DiskStreamer streamer(settings, nullptr);
        juce::String error;
        streamed = streamer.open(file, error);
        REQUIRE(streamed != nullptr);
        const auto arrangement = wholeFile(streamed);
        streamer.setPlan(*arrangement);
        streamer.followPlayhead([] { return DiskStreamer::Playhead{0, true}; });

        // The read-ahead is stuck in the disk: reads get silence, at once, and say so.
        std::vector<float> samples(512, 9.0f);
        bool complete = true;
        std::size_t heapOperations = 0;
        for (int i = 0; i < 100; ++i)
            heapOperations += daw::test::heapOperationsDuring(
                [&] { complete = streamed->read(0, i * 512, samples.data(), 512) && complete; });
        CHECK(heapOperations == 0);
        CHECK_FALSE(complete);
        CHECK(samples[511] == 0.0f);
        CHECK(streamer.getDropouts() == 100);
        CHECK_FALSE(streamed->isReady(0, 512));

        // The disk comes back.
        gate.signal();
        const auto deadline = juce::Time::getMillisecondCounter() + 10000;
        while (!streamed->isReady(0, 3 * blockLength) &&
               juce::Time::getMillisecondCounter() < deadline)
            juce::Thread::sleep(1);
        REQUIRE(streamed->isReady(0, 2 * blockLength));

        bool whole = false;
        CHECK(sameBits(readRange(*streamed, 0, 1000, 512, &whole),
                       readRange(*memory, 0, 1000, 512)));
        CHECK(whole);
        streamer.setPlan(Arrangement{});
        streamed.reset(); // before the streamer goes
    }
}

TEST_CASE("Streamed blocks stay within the budget and are reused only after the audio block",
          "[engine][streaming]")
{
    TemporaryDirectory directory;
    const auto file = writeTestFile(directory, "budget.wav", 1, 12 * blockLength, streamRate);

    auto settings = settingsFor(DiskStreamer::Mode::manual);
    settings.lookaheadSeconds = 0.5;
    settings.memoryBudget = 3 * blockLength * sizeof(float);
    daw::engine::RenderClock clock;
    DiskStreamer streamer(settings, &clock);
    juce::String error;
    const auto streamed = streamer.open(file, error);
    const auto arrangement = wholeFile(streamed);
    streamer.setPlan(*arrangement);

    DiskStreamer::Playhead playhead{0, true};
    streamer.followPlayhead([&playhead] { return playhead; });
    streamer.service();
    CHECK(streamed->isReady(0, blockLength));

    // An audio block is running: blocks dropped now may be in use, so they are not
    // reused, and nothing new can be loaded within the budget.
    clock.begin();
    for (int block = 3; block < 6; ++block)
    {
        playhead.position = block * blockLength;
        streamer.service();
    }
    CHECK(streamer.getMemoryInUse() <= settings.memoryBudget);
    CHECK_FALSE(streamed->isReady(5 * blockLength, blockLength));

    // Once it is over, the read-ahead catches up.
    clock.end();
    streamer.service();
    streamer.service();
    CHECK(streamed->isReady(5 * blockLength, blockLength));
    CHECK(streamer.getMemoryInUse() <= settings.memoryBudget);
}

TEST_CASE("Scanning a streamed file reads all of it, converted", "[engine][streaming]")
{
    TemporaryDirectory directory;
    const auto file = writeTestFile(directory, "scan.wav", 2, 2 * blockLength + 77, 44100.0);
    const auto memory = inMemory(file);

    DiskStreamer streamer(settingsFor(DiskStreamer::Mode::manual), nullptr);
    juce::String error;
    const auto streamed = streamer.open(file, error);

    std::vector<float> scanned;
    REQUIRE(streamer.scan(*streamed, [&](const juce::AudioBuffer<float>& block, int count) {
        scanned.insert(scanned.end(), block.getReadPointer(1), block.getReadPointer(1) + count);
    }));
    CHECK(sameBits(scanned, readRange(*memory, 1, 0, static_cast<int>(memory->getLength()))));
}
