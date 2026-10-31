#include "daw/engine/io/RecordingWriter.h"
#include "daw/engine/rt/AudioFifo.h"
#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"
#include "support/TestAudioFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>

#include <algorithm>
#include <atomic>
#include <bit>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <vector>

using daw::engine::io::RecordingSink;
using daw::engine::io::RecordingWriter;
using daw::engine::io::RecordingWriterSettings;
using daw::engine::rt::AudioFifo;
using daw::test::TemporaryDirectory;
using daw::test::testInputSample;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int waitMs = 60000;

bool waitUntil(const std::function<bool()>& condition, int timeoutMs = waitMs)
{
    const auto deadline =
        juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMs);
    while (!condition())
    {
        if (juce::Time::getMillisecondCounter() > deadline)
            return false;
        juce::Thread::sleep(1);
    }
    return true;
}

// Feeds a FIFO the way the audio thread would, a block at a time; the test thread
// waits when the FIFO is full (the real audio thread never does: see the overrun
// test). Samples come from signal(channel, index).
class Producer
{
public:
    Producer(AudioFifo& fifoToFill, std::function<float(int, juce::int64)> signalToPlay)
        : fifo(fifoToFill)
        , signal(std::move(signalToPlay))
        , block(fifoToFill.getNumChannels(), 4096)
    {
    }

    void push(juce::int64 numSamples)
    {
        const int sizes[] = {512, 1, 4096, 333, 64};
        for (juce::int64 done = 0; done < numSamples; ++next)
        {
            const int size =
                static_cast<int>(std::min<juce::int64>(sizes[next % 5], numSamples - done));
            for (int channel = 0; channel < block.getNumChannels(); ++channel)
                for (int i = 0; i < size; ++i)
                    block.setSample(channel, i, signal(channel, position + i));

            while (!fifo.push(block.getArrayOfReadPointers(), size))
                juce::Thread::sleep(1);

            position += size;
            done += size;
        }
    }

private:
    AudioFifo& fifo;
    std::function<float(int, juce::int64)> signal;
    juce::AudioBuffer<float> block;
    juce::int64 position = 0;
    std::size_t next = 0;
};

std::unique_ptr<juce::AudioFormatReader> openWav(const juce::File& file)
{
    juce::WavAudioFormat wav;
    return std::unique_ptr<juce::AudioFormatReader>(
        wav.createReaderFor(file.createInputStream().release(), true));
}

// True if the file holds exactly length samples of signal, bit for bit.
bool fileHolds(const juce::File& file, int numChannels, juce::int64 length,
               const std::function<float(int, juce::int64)>& signal)
{
    const auto reader = openWav(file);
    if (reader == nullptr || reader->lengthInSamples != length ||
        static_cast<int>(reader->numChannels) != numChannels)
        return false;

    constexpr int chunk = 1 << 20;
    juce::AudioBuffer<float> buffer(numChannels, chunk);
    for (juce::int64 start = 0; start < length; start += chunk)
    {
        const auto count = static_cast<int>(std::min<juce::int64>(chunk, length - start));
        if (!reader->read(&buffer, 0, count, start, true, true))
            return false;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float* samples = buffer.getReadPointer(channel);
            for (int i = 0; i < count; ++i)
                if (std::bit_cast<std::uint32_t>(samples[i]) !=
                    std::bit_cast<std::uint32_t>(signal(channel, start + i)))
                {
                    UNSCOPED_INFO("first difference: channel "
                                  << channel << ", sample " << start + i << ": " << samples[i]
                                  << " instead of " << signal(channel, start + i));
                    return false;
                }
        }
    }
    return true;
}

RecordingWriterSettings settingsFor(int bitsPerSample)
{
    RecordingWriterSettings settings;
    settings.sampleRate = sampleRate;
    settings.bitsPerSample = bitsPerSample;
    settings.freeSpace = [](const juce::File&) { return juce::int64{1} << 40; };
    return settings;
}

// A disk that takes data only when the test lets it, and can be told to fail.
struct SimulatedDisk
{
    SimulatedDisk() { open.signal(); }

    juce::WaitableEvent open{true}; // signalled: the disk takes data
    std::atomic<juce::int64> received{0};
    std::atomic<juce::int64> capacity{std::numeric_limits<juce::int64>::max()};
    std::atomic<int> sinksOpen{0};

    class Sink final : public RecordingSink
    {
    public:
        explicit Sink(SimulatedDisk& diskToUse)
            : disk(diskToUse)
        {
            ++disk.sinksOpen;
        }

        ~Sink() override { --disk.sinksOpen; }

        bool write(const juce::AudioBuffer<float>&, int numSamples) override
        {
            disk.open.wait(-1);
            if (disk.received + numSamples > disk.capacity)
                return false;
            disk.received += numSamples;
            return true;
        }

        bool flush() override { return true; }

    private:
        SimulatedDisk& disk;
    };
};

} // namespace

TEST_CASE("The audio FIFO passes whole blocks through and refuses what does not fit",
          "[engine][recording]")
{
    AudioFifo fifo(2, 1000);
    std::vector<float> left(700);
    std::vector<float> right(700);
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        left[i] = static_cast<float>(i);
        right[i] = -static_cast<float>(i);
    }
    const float* both[] = {left.data(), right.data()};
    const float* onlyLeft[] = {left.data(), nullptr};

    bool pushed = false;
    bool refused = false;
    const auto heapOperations = daw::test::heapOperationsDuring([&] {
        pushed = fifo.push(both, 700);
        refused = !fifo.push(both, 301); // 700 + 301 > 1000: nothing is taken
    });
    CHECK(heapOperations == 0);
    CHECK(pushed);
    CHECK(refused);
    CHECK(fifo.getNumReady() == 700);

    std::vector<float> outLeft(1000);
    std::vector<float> outRight(1000);
    float* out[] = {outLeft.data(), outRight.data()};
    CHECK(fifo.pop(out, 500) == 500);
    CHECK(outLeft[499] == 499.0f);

    // Wraps around the end of the storage; a null channel is silence.
    REQUIRE(fifo.push(onlyLeft, 700));
    CHECK(fifo.getNumReady() == 900);
    CHECK(fifo.pop(out, 1000) == 900);
    CHECK(outLeft[199] == 699.0f);
    CHECK(outLeft[200] == 0.0f);
    CHECK(outLeft[899] == 699.0f);
    CHECK(outRight[199] == -699.0f);
    CHECK(outRight[899] == 0.0f);
    CHECK(fifo.pop(out, 10) == 0);
}

TEST_CASE("Waveform peaks built a block at a time equal those of the whole buffer",
          "[engine][recording]")
{
    juce::AudioBuffer<float> audio(2, 10000);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample(channel, i, testInputSample(channel, i));

    const auto whole = daw::engine::dsp::WaveformPeaks::compute(audio);
    daw::engine::dsp::WaveformPeaks::Builder builder(2);
    for (int done = 0, size = 1; done < audio.getNumSamples(); size = size * 3 % 1000 + 1)
    {
        const int count = std::min(size, audio.getNumSamples() - done);
        const float* channels[] = {audio.getReadPointer(0, done), audio.getReadPointer(1, done)};
        builder.append(channels, count);
        done += count;
    }
    const auto pieces = builder.build();

    REQUIRE(pieces.getNumBuckets() == whole.getNumBuckets());
    CHECK(pieces.getNumSamples() == 10000);
    bool same = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int bucket = 0; bucket < whole.getNumBuckets(); ++bucket)
        {
            const auto piece = pieces.getBucket(channel, bucket);
            const auto all = whole.getBucket(channel, bucket);
            same = same && piece.min == all.min && piece.max == all.max;
        }
    CHECK(same);
}

TEST_CASE("Recorded files hold the input bit for bit", "[engine][recording]")
{
    const int bits = GENERATE(16, 24, 32);
    CAPTURE(bits);

    // Values the format holds exactly: what a converter of that resolution delivers
    // (any float for 32-bit float files).
    const auto signal = [bits](int channel, juce::int64 index) {
        const float sample = testInputSample(channel, index);
        if (bits == 32)
            return sample * 1.5f; // beyond full scale too: float files never clip
        // Integers have no -0: a converter delivers +0.
        const float scale = std::ldexp(1.0f, bits - 1);
        return std::min(std::round(sample * scale), scale - 1.0f) / scale + 0.0f;
    };

    TemporaryDirectory directory;
    RecordingWriter writer(settingsFor(bits));
    auto& stereo = writer.addFile(directory.file("stereo.wav"), 2);
    auto& mono = writer.addFile(directory.file("mono.wav"), 1);
    REQUIRE(writer.start().wasOk());

    Producer stereoProducer(stereo, signal);
    Producer monoProducer(mono, [&](int, juce::int64 index) { return signal(5, index); });
    stereoProducer.push(100000);
    monoProducer.push(77777);
    writer.finish();

    REQUIRE(waitUntil([&] { return writer.isFinished(); }));
    CHECK(writer.getProblem() == RecordingWriter::Problem::none);
    CHECK(writer.getSamplesWritten(0) == 100000);
    CHECK(writer.getSamplesWritten(1) == 77777);
    CHECK(writer.getPeaks(0)->getNumSamples() == 100000);
    CHECK(fileHolds(directory.file("stereo.wav"), 2, 100000, signal));
    CHECK(fileHolds(directory.file("mono.wav"), 1, 77777,
                    [&](int, juce::int64 index) { return signal(5, index); }));
}

TEST_CASE("An hour of recording loses and repeats no sample", "[engine][recording][long]")
{
    const auto hour = static_cast<juce::int64>(sampleRate) * 3600;

    TemporaryDirectory directory;
    RecordingWriter writer(settingsFor(24));
    auto& fifo = writer.addFile(directory.file("hour.wav"), 1);
    REQUIRE(writer.start().wasOk());

    Producer producer(fifo, testInputSample);
    producer.push(hour);
    writer.finish();

    REQUIRE(waitUntil([&] { return writer.isFinished(); }));
    CHECK(writer.getProblem() == RecordingWriter::Problem::none);
    CHECK(writer.getSamplesWritten(0) == hour);
    CHECK(writer.getPeaks(0)->getNumSamples() == hour);
    CHECK(fileHolds(directory.file("hour.wav"), 1, hour, testInputSample));
}

TEST_CASE("A slow disk overruns the FIFO without ever making the audio thread wait",
          "[engine][recording]")
{
    SimulatedDisk disk;
    disk.open.reset(); // the disk stalls

    auto settings = settingsFor(24);
    settings.bufferSeconds = 0.1; // 4800 samples
    settings.openSink = [&disk](const juce::File&, int, juce::String&) {
        return std::make_unique<SimulatedDisk::Sink>(disk);
    };

    TemporaryDirectory directory;
    RecordingWriter writer(settings);
    auto& fifo = writer.addFile(directory.file("slow.wav"), 1);
    REQUIRE(writer.start().wasOk());

    // The audio thread pushes a block every 512 samples; the first one that does not
    // fit is refused at once (a waiting push would never return: the disk is stuck).
    std::vector<float> block(512, 0.25f);
    const float* channels[] = {block.data()};
    juce::int64 accepted = 0;
    bool overrun = false;
    std::size_t heapOperations = 0;
    for (int i = 0; i < 1000 && !overrun; ++i)
    {
        bool pushed = false;
        heapOperations +=
            daw::test::heapOperationsDuring([&] { pushed = fifo.push(channels, 512); });
        if (pushed)
            accepted += 512;
        else
            overrun = true;
    }

    CHECK(heapOperations == 0);
    CHECK(overrun);
    CHECK(accepted >= 4608);  // at least the FIFO's worth of whole blocks
    CHECK(accepted <= 10240); // plus what the writer took before the disk stalled

    // When the disk comes back, exactly the accepted samples reach it.
    disk.open.signal();
    writer.finish();
    REQUIRE(waitUntil([&] { return writer.isFinished(); }));
    CHECK(disk.received == accepted);
    CHECK(writer.getSamplesWritten(0) == accepted);
    CHECK(disk.sinksOpen == 0);
}

TEST_CASE("A disk that fills up is reported and keeps what was written", "[engine][recording]")
{
    SimulatedDisk disk;
    disk.capacity = 30000;

    auto settings = settingsFor(24);
    settings.openSink = [&disk](const juce::File&, int, juce::String&) {
        return std::make_unique<SimulatedDisk::Sink>(disk);
    };

    TemporaryDirectory directory;
    RecordingWriter writer(settings);
    auto& fifo = writer.addFile(directory.file("full.wav"), 1);
    REQUIRE(writer.start().wasOk());

    Producer producer(fifo, testInputSample);
    producer.push(100000); // the writer keeps emptying the FIFO after the failure
    writer.finish();

    REQUIRE(waitUntil([&] { return writer.isFinished(); }));
    CHECK(writer.getProblem() == RecordingWriter::Problem::writeFailed);
    CHECK(writer.getSamplesWritten(0) == disk.received);
    CHECK(disk.received <= 30000);
    CHECK(disk.received > 30000 - 4800);
}

TEST_CASE("Little free disk space asks for recording to stop", "[engine][recording]")
{
    std::atomic<juce::int64> freeBytes{1'000'000'000};
    auto settings = settingsFor(24);
    settings.checkIntervalMilliseconds = 5;
    settings.freeSpace = [&](const juce::File&) { return freeBytes.load(); };

    TemporaryDirectory directory;
    RecordingWriter writer(settings);
    writer.addFile(directory.file("a.wav"), 2);
    writer.addFile(directory.file("b.wav"), 1);
    REQUIRE(writer.start().wasOk());

    // Three channels at 48 kHz, 24-bit: 432,000 bytes a second.
    CHECK(RecordingWriter::bytesPerSecond(sampleRate, 24, 3) == 432000.0);
    REQUIRE(waitUntil([&] { return writer.getSecondsLeft() > 0.0; }));
    CHECK(writer.getSecondsLeft() == 1'000'000'000.0 / 432000.0);
    CHECK(writer.getProblem() == RecordingWriter::Problem::none);

    freeBytes = 432000 * 9; // nine seconds
    REQUIRE(waitUntil([&] { return writer.getProblem() != RecordingWriter::Problem::none; }));
    CHECK(writer.getProblem() == RecordingWriter::Problem::lowDiskSpace);

    writer.discard();
    CHECK_FALSE(directory.file("a.wav").exists());
    CHECK_FALSE(directory.file("b.wav").exists());
}

TEST_CASE("Files being recorded are readable up to the last check", "[engine][recording]")
{
    auto settings = settingsFor(24);
    settings.checkIntervalMilliseconds = 10;

    TemporaryDirectory directory;
    RecordingWriter writer(settings);
    auto& fifo = writer.addFile(directory.file("live.wav"), 1);
    REQUIRE(writer.start().wasOk());

    Producer producer(fifo, testInputSample);
    producer.push(48000);
    REQUIRE(waitUntil([&] { return writer.getSamplesWritten(0) == 48000; }));

    // What a crash would leave behind: the file as it is on disk, once a check has
    // come after the last write (every 10 ms here, whenever the thread gets to it).
    const auto copy = directory.file("copy.wav");
    CHECK(waitUntil(
        [&] {
            return directory.file("live.wav").copyFileTo(copy) &&
                   fileHolds(copy, 1, 48000, testInputSample);
        },
        10000));

    writer.finish();
    REQUIRE(waitUntil([&] { return writer.isFinished(); }));
}
