#include "daw/engine/AudioSource.h"
#include "daw/engine/io/SourcePool.h"

#include "support/RealtimeCheck.h"
#include "support/SignalAnalysis.h"
#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <vector>

using Catch::Matchers::ContainsSubstring;
using daw::engine::AudioData;
using daw::engine::MemoryAudioSource;
using daw::engine::io::PooledSource;
using daw::engine::io::SourcePool;
using daw::test::AudioFileType;
using daw::test::TemporaryDirectory;

namespace
{

constexpr int waitMilliseconds = 30000;

MemoryAudioSource makeRamp(int length)
{
    AudioData data;
    data.sampleRate = 48000.0;
    data.samples.setSize(2, length);
    for (int n = 0; n < length; ++n)
    {
        data.samples.setSample(0, n, static_cast<float>(n + 1));
        data.samples.setSample(1, n, -static_cast<float>(n + 1));
    }
    return MemoryAudioSource(std::move(data));
}

juce::File writeTone(const TemporaryDirectory& directory, const juce::String& name,
                     double sampleRate, int numSamples)
{
    const auto file = directory.file(name);
    const auto tone = daw::test::makeSine(2, numSamples, 440.0, sampleRate, 0.5);
    REQUIRE(daw::test::writeAudioFile(file, AudioFileType::wav, tone, sampleRate, 24));
    return file;
}

void finishLoads(SourcePool& pool)
{
    REQUIRE(pool.waitForLoads(waitMilliseconds));
    pool.collectFinishedLoads();
}

} // namespace

TEST_CASE("A memory source reads its samples and silence around them", "[engine][source]")
{
    const auto source = makeRamp(10);
    CHECK(source.getNumChannels() == 2);
    CHECK(source.getLength() == 10);
    CHECK(source.getSampleRate() == 48000.0);

    std::vector<float> buffer(8, 99.0f);
    bool delivered = false;

    const auto heapOperations = daw::test::heapOperationsDuring(
        [&] { delivered = source.read(1, -3, buffer.data(), 8); });
    CHECK(heapOperations == 0);
    CHECK(delivered);
    CHECK(buffer == std::vector<float>{0, 0, 0, -1, -2, -3, -4, -5});

    REQUIRE(source.read(0, 6, buffer.data(), 8));
    CHECK(buffer == std::vector<float>{7, 8, 9, 10, 0, 0, 0, 0});

    REQUIRE(source.read(0, 50, buffer.data(), 8));
    CHECK(buffer == std::vector<float>(8, 0.0f));

    REQUIRE(source.read(0, -20, buffer.data(), 8));
    CHECK(buffer == std::vector<float>(8, 0.0f));

    REQUIRE(source.read(0, -2, buffer.data(), 8)); // a source shorter than the read
    std::vector<float> longer(16, 99.0f);
    REQUIRE(source.read(0, -2, longer.data(), 16));
    CHECK(longer == std::vector<float>{0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 0, 0, 0, 0});
}

TEST_CASE("A file used by several clips is loaded once", "[engine][source]")
{
    TemporaryDirectory directory;
    const auto file = writeTone(directory, "tone.wav", 48000.0, 4800);

    SourcePool pool(48000.0);
    std::vector<juce::File> changed;
    pool.onSourceChanged = [&changed](const juce::File& f) { changed.push_back(f); };

    pool.request(file);
    pool.request(file);
    REQUIRE(pool.find(file) != nullptr);
    CHECK(pool.find(file)->status == PooledSource::Status::loading);
    finishLoads(pool);
    pool.request(file);

    CHECK(pool.getNumLoadsStarted() == 1);
    CHECK(changed == std::vector<juce::File>{file});

    const auto* source = pool.find(file);
    REQUIRE(source != nullptr);
    REQUIRE(source->status == PooledSource::Status::ready);
    CHECK(source->audio->getLength() == 4800);
    CHECK(source->audio->getNumChannels() == 2);
    CHECK(source->audio->getSampleRate() == 48000.0);
    CHECK(source->peaks->getNumSamples() == 4800);
    CHECK(source->fileSampleRate == 48000.0);
    CHECK(source->bitsPerSample == 24);
    CHECK(source->formatName.containsIgnoreCase("WAV"));
}

TEST_CASE("Files are converted to the pool's sample rate, again when it changes",
          "[engine][source]")
{
    TemporaryDirectory directory;
    const auto file = writeTone(directory, "tone.wav", 44100.0, 44100);

    SourcePool pool(48000.0);
    pool.request(file);
    finishLoads(pool);
    REQUIRE(pool.find(file)->status == PooledSource::Status::ready);
    CHECK(pool.find(file)->audio->getLength() == 48000);
    CHECK(pool.find(file)->audio->getSampleRate() == 48000.0);

    pool.setSampleRate(96000.0);
    CHECK(pool.find(file)->status == PooledSource::Status::loading);
    CHECK(pool.find(file)->audio == nullptr);
    finishLoads(pool);

    CHECK(pool.getNumLoadsStarted() == 2);
    REQUIRE(pool.find(file)->status == PooledSource::Status::ready);
    CHECK(pool.find(file)->audio->getLength() == 96000);
    CHECK(pool.find(file)->audio->getSampleRate() == 96000.0);
}

TEST_CASE("Only the latest load of a file counts", "[engine][source]")
{
    TemporaryDirectory directory;
    const auto file = writeTone(directory, "tone.wav", 48000.0, 4800);

    SourcePool pool(48000.0);
    pool.request(file);
    pool.setSampleRate(24000.0); // before the first load has been collected
    finishLoads(pool);

    REQUIRE(pool.find(file)->status == PooledSource::Status::ready);
    CHECK(pool.find(file)->audio->getSampleRate() == 24000.0);
    CHECK(pool.find(file)->audio->getLength() == 2400);

    // Results of loads started before clear() are dropped.
    pool.reload(file);
    pool.clear();
    finishLoads(pool);
    CHECK(pool.find(file) == nullptr);
}

TEST_CASE("A missing or unreadable file fails with a message", "[engine][source]")
{
    TemporaryDirectory directory;
    const auto missing = directory.file("missing.wav");
    const auto notAudio = directory.file("text.wav");
    REQUIRE(notAudio.replaceWithText("not audio"));

    SourcePool pool(48000.0);
    pool.request(missing);
    pool.request(notAudio);
    finishLoads(pool);

    REQUIRE(pool.find(missing)->status == PooledSource::Status::failed);
    CHECK_THAT(pool.find(missing)->error.toStdString(), ContainsSubstring("does not exist"));
    CHECK(pool.find(missing)->audio == nullptr);
    CHECK(pool.find(notAudio)->status == PooledSource::Status::failed);

    // Failed files are not retried by request(), only by reload().
    pool.request(missing);
    CHECK(pool.getNumLoadsStarted() == 2);

    writeTone(directory, "missing.wav", 48000.0, 480);
    pool.reload(missing);
    finishLoads(pool);
    CHECK(pool.find(missing)->status == PooledSource::Status::ready);
}
