#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/Session.h"
#include "daw/engine/io/AudioFileLoader.h"
#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"
#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

#include <bit>
#include <cstdint>
#include <map>
#include <memory>
#include <vector>

using daw::controller::Session;
using daw::engine::Engine;
using daw::engine::io::DiskStreamer;
using daw::model::secondsToTicks;
using daw::test::TemporaryDirectory;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

juce::File writeFile(const TemporaryDirectory& directory, const juce::String& name,
                     int numChannels, double seconds, double rate)
{
    juce::AudioBuffer<float> audio(numChannels, static_cast<int>(seconds * rate));
    for (int channel = 0; channel < numChannels; ++channel)
        for (int i = 0; i < audio.getNumSamples(); ++i)
            audio.setSample(channel, i, daw::test::testInputSample(channel, i) * 0.5f);

    const auto file = directory.file(name);
    REQUIRE(daw::test::writeAudioFile(file, daw::test::AudioFileType::wav, audio, rate, 24));
    return file;
}

// The session's engine (streaming) and a reference engine playing the same project
// from files decoded whole into memory, run side by side.
struct SideBySide
{
    SideBySide()
    {
        streaming.prepare(sampleRate, blockSize);
        memory.prepare(sampleRate, blockSize);
    }

    void startReference()
    {
        std::map<juce::String, std::shared_ptr<const daw::engine::AudioSource>> decoded;
        const auto lookup = [&decoded](const juce::File& file) {
            auto& source = decoded[file.getFullPathName()];
            if (source == nullptr)
            {
                auto loaded = daw::engine::io::loadAudioFile(file, sampleRate);
                source = std::make_shared<const daw::engine::MemoryAudioSource>(
                    std::move(*loaded.audio));
            }
            return source;
        };
        memory.setArrangement(
            daw::controller::buildArrangement(session.getProject(), sampleRate, lookup));
    }

    // Runs both for numBlocks; false if they ever differ.
    bool run(int numBlocks)
    {
        bool same = true;
        juce::AudioBuffer<float> fromDisk(2, blockSize);
        juce::AudioBuffer<float> fromMemory(2, blockSize);
        for (int i = 0; i < numBlocks; ++i)
        {
            heapOperations += daw::test::heapOperationsDuring([&] { streaming.process(fromDisk); });
            memory.process(fromMemory);
            session.update(); // the read-ahead runs here, as its thread would

            for (int channel = 0; channel < 2; ++channel)
                for (int s = 0; s < blockSize; ++s)
                    same = same &&
                           std::bit_cast<std::uint32_t>(fromDisk.getSample(channel, s)) ==
                               std::bit_cast<std::uint32_t>(fromMemory.getSample(channel, s));
        }
        return same;
    }

    void seek(double seconds)
    {
        session.seek(secondsToTicks(seconds));
        memory.seek(daw::model::ticksToSamples(secondsToTicks(seconds), sampleRate));
        session.update(); // the click on the ruler happens a moment before the block
    }

    Engine streaming;
    Engine memory;
    Session session{streaming, {DiskStreamer::Mode::manual, std::size_t{16} << 20}};
    std::size_t heapOperations = 0;
};

} // namespace

TEST_CASE("Streamed playback equals playback from memory bit for bit, with loops and seeks",
          "[controller][streaming]")
{
    TemporaryDirectory directory;
    const auto stereo = writeFile(directory, "stereo.wav", 2, 9.0, 48000.0);
    const auto converted = writeFile(directory, "converted.wav", 1, 7.0, 44100.0);

    SideBySide both;
    auto& session = both.session;
    session.importAudio({stereo}, 0);
    session.importAudio({converted}, secondsToTicks(0.5));
    session.setLoopRange(secondsToTicks(1.2), secondsToTicks(2.0));
    REQUIRE(session.waitForLoads(30000));
    both.startReference();

    session.play();
    both.memory.play();
    CHECK(both.run(100));

    both.seek(6.5);
    CHECK(both.run(150));

    session.setLoopEnabled(true);
    both.memory.setLooping(true);
    both.seek(1.0);
    CHECK(both.run(400)); // round the loop about five times

    session.stop();
    both.memory.stop();
    CHECK(both.run(10));

    CHECK(both.heapOperations == 0);
    CHECK(session.takeDropouts() == 0);
    CHECK(session.getEngine().getTransportState() == daw::engine::TransportState::stopped);
    // A 16 MB budget holds about 40 seconds of stereo: these files fit, the memory
    // never grows beyond it.
    CHECK(session.getProject().getNumTracks() == 2);
}

TEST_CASE("Playback starting on audio still loading waits for it", "[controller][streaming]")
{
    TemporaryDirectory directory;
    const auto file = writeFile(directory, "long.wav", 2, 12.0, 48000.0);

    Engine engine;
    engine.prepare(sampleRate, blockSize);
    Session session(engine, {DiskStreamer::Mode::manual, std::size_t{16} << 20});
    session.importAudio({file}, 0);
    REQUIRE(session.waitForLoads(30000));

    // Jump far and play at once, before the read-ahead has had a turn.
    engine.seek(10 * 48000);
    engine.play();
    juce::AudioBuffer<float> block(2, blockSize);
    engine.process(block);
    CHECK(engine.isWaitingForData());
    CHECK(engine.getTransportState() == daw::engine::TransportState::playing); // as asked
    CHECK(block.getMagnitude(0, blockSize) == 0.0f);

    session.update(); // the read-ahead loads the start
    engine.process(block);
    CHECK_FALSE(engine.isWaitingForData());
    CHECK(block.getMagnitude(0, blockSize) > 0.0f);
    CHECK(session.takeDropouts() == 0);
}

TEST_CASE("An edit during playback waits for the audio it plays", "[controller][streaming]")
{
    TemporaryDirectory directory;
    const auto file = writeFile(directory, "long.wav", 1, 20.0, 48000.0);

    Engine engine;
    engine.prepare(sampleRate, blockSize);
    Session session(engine, {DiskStreamer::Mode::manual, std::size_t{8} << 20});
    const auto clips = session.importAudio({file}, 0);
    REQUIRE(session.waitForLoads(30000));

    session.play();
    juce::AudioBuffer<float> block(2, blockSize);
    for (int i = 0; i < 50; ++i)
    {
        engine.process(block);
        session.update();
    }

    // Trim the clip so that the playhead now plays from 15 s into the file: not loaded.
    const auto playhead = session.getPlayhead();
    session.setClipBounds(clips[0], {0, secondsToTicks(15.0) - playhead, secondsToTicks(5.0)});
    for (int i = 0; i < 50; ++i)
    {
        engine.process(block);
        session.update();
    }

    CHECK(session.takeDropouts() == 0);
    CHECK(block.getMagnitude(0, blockSize) > 0.0f);
}
