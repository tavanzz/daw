#include "daw/controller/Session.h"
#include "daw/model/ProjectFile.h"
#include "support/SimulatedDevice.h"
#include "support/TestArrangements.h"
#include "support/TestAudioFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <atomic>
#include <bit>
#include <cstdint>
#include <memory>

using Catch::Matchers::ContainsSubstring;
using daw::controller::Session;
using daw::model::ObjectId;
using daw::model::Ticks;
using daw::model::ticksPerSecond;
using daw::test::TemporaryDirectory;
using daw::test::testInputSample;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;

struct Studio
{
    Studio()
    {
        device.inputSignal = testInputSample;
        device.start();
    }

    /** Runs the audio device, then the session's update (the UI timer). */
    std::size_t run(juce::int64 numSamples)
    {
        const auto heapOperations = device.run(numSamples);
        session.update();
        return heapOperations;
    }

    /** Runs until the recording's clips are in the project. */
    bool finishRecording()
    {
        for (int i = 0; i < 20000 && session.isRecording(); ++i)
        {
            run(blockSize);
            juce::Thread::sleep(1);
        }
        return !session.isRecording();
    }

    juce::File projectFile() const { return directory.file("song.dawproject"); }

    daw::engine::Engine engine;
    daw::test::SimulatedDevice device{engine, sampleRate, 4, {blockSize}};
    Session session{engine};
    TemporaryDirectory directory;
};

std::optional<daw::model::Clip> onlyClip(const Session& session, ObjectId trackId)
{
    const auto& project = session.getProject();
    const auto track = project.getTrack(project.indexOfTrack(trackId));
    if (track.getNumClips() != 1)
        return std::nullopt;
    return track.getClip(0);
}

// True if the file holds signal(channel, firstIndex + k) at sample k, bit for bit.
bool fileHolds(const juce::File& file, int numChannels, juce::int64 length,
               const std::function<float(int, juce::int64)>& signal)
{
    juce::WavAudioFormat wav;
    const std::unique_ptr<juce::AudioFormatReader> reader(
        wav.createReaderFor(file.createInputStream().release(), true));
    if (reader == nullptr || reader->lengthInSamples != length ||
        static_cast<int>(reader->numChannels) != numChannels)
        return false;

    juce::AudioBuffer<float> audio(numChannels, static_cast<int>(length));
    reader->read(&audio, 0, static_cast<int>(length), 0, true, true);
    for (int channel = 0; channel < numChannels; ++channel)
        for (int k = 0; k < static_cast<int>(length); ++k)
            if (std::bit_cast<std::uint32_t>(audio.getSample(channel, k)) !=
                std::bit_cast<std::uint32_t>(signal(channel, k)))
                return false;
    return true;
}

Ticks seconds(double value)
{
    return daw::model::secondsToTicks(value);
}

} // namespace

TEST_CASE("A recording becomes one clip per armed track, in one undo step",
          "[controller][recording]")
{
    Studio studio;
    auto& session = studio.session;
    const auto vocals = session.addTrack("Vocals"); // input 1, mono
    const auto guitar = session.addTrack("Guitar");
    const auto idle = session.addTrack("Idle");
    session.setTrackInput(guitar, {2, true});
    session.setTrackArmed(vocals, true);
    session.setTrackArmed(guitar, true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());

    session.seek(ticksPerSecond);
    studio.run(blockSize);
    const juce::int64 recordingStart = studio.device.getStreamPosition();

    const auto undoBefore = session.getUndoDescription();
    REQUIRE(session.record().wasOk());
    CHECK(session.isRecording());
    std::size_t heapOperations = studio.run(2 * 48000);
    session.stop();
    REQUIRE(studio.finishRecording());
    CHECK(heapOperations == 0);
    CHECK(session.takeRecordingProblem().isEmpty());

    // One clip each on the armed tracks, from where the playhead was, as long as the
    // recording ran (no latency to compensate here).
    const auto vocalsClip = onlyClip(session, vocals);
    const auto guitarClip = onlyClip(session, guitar);
    REQUIRE(vocalsClip);
    REQUIRE(guitarClip);
    CHECK(vocalsClip->getBounds() == daw::model::ClipBounds{ticksPerSecond, 0, 2 * ticksPerSecond});
    CHECK(guitarClip->getBounds() == vocalsClip->getBounds());
    CHECK(session.getProject().getTrack(2).getNumClips() == 0);
    juce::ignoreUnused(idle);

    // In the project's Audio folder, holding exactly the inputs.
    const auto audioFolder = studio.directory.file("Audio");
    CHECK(vocalsClip->getAudioFile() == audioFolder.getChildFile("Vocals 1.wav"));
    CHECK(guitarClip->getAudioFile() == audioFolder.getChildFile("Guitar 1.wav"));
    CHECK(fileHolds(vocalsClip->getAudioFile(), 1, 96000, [&](int, juce::int64 k) {
        return testInputSample(0, recordingStart + k);
    }));
    CHECK(fileHolds(guitarClip->getAudioFile(), 2, 96000, [&](int channel, juce::int64 k) {
        return testInputSample(2 + channel, recordingStart + k);
    }));

    // Saved relative to the project.
    const auto json = daw::model::toJson(session.getProject(), studio.directory.file(""));
    CHECK_THAT(json.toStdString(), ContainsSubstring("\"audioFile\": \"Audio/Vocals 1.wav\""));

    // One undo step removes both; the files stay for redo.
    CHECK(session.getUndoDescription() == "Record");
    REQUIRE(session.undo());
    CHECK(session.getUndoDescription() == undoBefore);
    CHECK_FALSE(onlyClip(session, vocals));
    CHECK_FALSE(onlyClip(session, guitar));
    CHECK(audioFolder.getChildFile("Vocals 1.wav").existsAsFile());
    REQUIRE(session.redo());
    CHECK(onlyClip(session, guitar));

    // The clips play from the new files.
    CHECK(session.waitForLoads(30000));
    CHECK(session.getFileLength(vocalsClip->getAudioFile()) == 2 * ticksPerSecond);
}

TEST_CASE("Recording needs a saved project, an armed track, a loop to punch and disk space",
          "[controller][recording]")
{
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack();

    CHECK_THAT(session.record().getErrorMessage().toStdString(), ContainsSubstring("Save the project"));
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());
    CHECK_THAT(session.record().getErrorMessage().toStdString(), ContainsSubstring("Arm a track"));

    session.setTrackArmed(track, true);
    session.setTrackInput(track, daw::model::TrackInput::none());
    CHECK_THAT(session.record().getErrorMessage().toStdString(), ContainsSubstring("Arm a track"));
    session.setTrackInput(track, {0, false});

    session.setPunchEnabled(true);
    CHECK_THAT(session.record().getErrorMessage().toStdString(), ContainsSubstring("loop region"));
    session.setPunchEnabled(false);

    // 50 seconds of mono 24-bit at 48 kHz: not the minute needed.
    session.getRecording().freeSpace = [](const juce::File&) { return juce::int64{50 * 144000}; };
    CHECK_THAT(session.record().getErrorMessage().toStdString(), ContainsSubstring("disk space"));
    CHECK_FALSE(session.isRecording());
    CHECK_FALSE(studio.directory.file("Audio").getChildFile("Track 1 1.wav").exists());
}

TEST_CASE("A discarded recording leaves no clip and no file", "[controller][recording]")
{
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Take");
    session.setTrackArmed(track, true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());
    const auto undoBefore = session.getUndoDescription();

    REQUIRE(session.record().wasOk());
    studio.run(48000);
    CHECK(studio.directory.file("Audio").getChildFile("Take 1.wav").existsAsFile());
    session.discardRecording();
    REQUIRE(studio.finishRecording());

    CHECK_FALSE(onlyClip(session, track));
    CHECK_FALSE(studio.directory.file("Audio").getChildFile("Take 1.wav").exists());
    CHECK(session.getUndoDescription() == undoBefore);
}

TEST_CASE("Recordings never overwrite a file", "[controller][recording]")
{
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Bass/DI"); // not a legal file name as it is
    session.setTrackArmed(track, true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());

    const auto audioFolder = studio.directory.file("Audio");
    REQUIRE(audioFolder.createDirectory());
    const auto existing = audioFolder.getChildFile(juce::File::createLegalFileName("Bass/DI") + " 1.wav");
    REQUIRE(existing.replaceWithText("not audio"));

    REQUIRE(session.record().wasOk());
    studio.run(10000);
    session.stop();
    REQUIRE(studio.finishRecording());

    const auto clip = onlyClip(session, track);
    REQUIRE(clip);
    CHECK(clip->getAudioFile().getFileName() ==
          juce::File::createLegalFileName("Bass/DI") + " 2.wav");
    CHECK(existing.loadFileAsString() == "not audio");
}

TEST_CASE("Punch keeps the loop region; the file keeps the rest", "[controller][recording]")
{
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Punch");
    session.setTrackArmed(track, true);
    session.setLoopRange(seconds(2.0), seconds(3.0));
    session.setPunchEnabled(true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());

    session.seek(seconds(1.0));
    studio.run(blockSize);
    REQUIRE(session.record().wasOk());
    studio.run(3 * 48000);
    session.stop();
    REQUIRE(studio.finishRecording());

    const auto clip = onlyClip(session, track);
    REQUIRE(clip);
    CHECK(clip->getBounds() == daw::model::ClipBounds{seconds(2.0), seconds(1.0), seconds(1.0)});
    CHECK(session.getFileLength(clip->getAudioFile()) == std::nullopt); // still loading
    REQUIRE(session.waitForLoads(30000));
    CHECK(session.getFileLength(clip->getAudioFile()) == seconds(3.0));
}

TEST_CASE("The count-in is recorded before the clip, which starts at the playhead",
          "[controller][recording]")
{
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Count");
    session.setTrackArmed(track, true);
    session.setCountInEnabled(true); // one bar: 2 s at 120 BPM in 4/4
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());

    session.seek(seconds(4.0));
    studio.run(blockSize);
    REQUIRE(session.record().wasOk());
    studio.run(blockSize);
    CHECK(session.getRecording().getState() ==
          daw::controller::RecordingController::State::countingIn);
    studio.run(3 * 48000);
    CHECK(session.getRecording().getState() ==
          daw::controller::RecordingController::State::recording);
    session.stop();
    REQUIRE(studio.finishRecording());

    const auto clip = onlyClip(session, track);
    REQUIRE(clip);
    CHECK(clip->getPosition() == seconds(4.0));
    CHECK(clip->getOffset() == seconds(2.0));
    CHECK(session.getPlayhead() == seconds(4.0)); // stop returns to where recording started
}

TEST_CASE("A disk that cannot keep up stops the recording and keeps what was captured",
          "[controller][recording]")
{
    struct StuckDisk final : daw::engine::io::RecordingSink
    {
        explicit StuckDisk(juce::WaitableEvent& gateToWaitFor)
            : gate(gateToWaitFor)
        {
        }
        bool write(const juce::AudioBuffer<float>&, int) override { return gate.wait(-1); }
        bool flush() override { return true; }
        juce::WaitableEvent& gate;
    };

    juce::WaitableEvent gate(true);
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Slow");
    session.setTrackArmed(track, true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());
    session.getRecording().adjustWriterSettings = [&gate](auto& settings) {
        settings.bufferSeconds = 0.05; // 2400 samples
        settings.openSink = [&gate](const juce::File&, int, juce::String&) {
            return std::make_unique<StuckDisk>(gate);
        };
    };

    REQUIRE(session.record().wasOk());
    studio.run(20 * blockSize); // the update after the overrun stops the transport...
    studio.run(blockSize);      // ...at the next block
    CHECK(session.getTransportState() == daw::engine::TransportState::stopped);
    gate.signal();
    REQUIRE(studio.finishRecording());

    const auto problem = session.takeRecordingProblem();
    CHECK_THAT(problem.toStdString(), ContainsSubstring("could not keep up"));
    const auto clip = onlyClip(session, track);
    REQUIRE(clip);
    const auto captured = daw::model::ticksToSamples(*clip->getLength(), sampleRate);
    CHECK(captured >= 2048); // what fit in the FIFO, plus what the writer took first
    CHECK(captured < 20 * blockSize);
    CHECK(captured % blockSize == 0); // whole blocks: the overrun cut on a block boundary
}

TEST_CASE("A nearly full disk stops the recording with a message", "[controller][recording]")
{
    std::atomic<juce::int64> freeBytes{juce::int64{1} << 40};
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Long");
    session.setTrackArmed(track, true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());
    session.getRecording().freeSpace = [&](const juce::File&) { return freeBytes.load(); };
    session.getRecording().adjustWriterSettings = [](auto& settings) {
        settings.checkIntervalMilliseconds = 1;
    };

    REQUIRE(session.record().wasOk());
    studio.run(48000);
    juce::Thread::sleep(20);
    CHECK(session.getRecording().getSecondsLeft() > 1.0e6);

    freeBytes = 144000 * 5; // five seconds left
    for (int i = 0; i < 2000 && session.getTransportState() != daw::engine::TransportState::stopped; ++i)
    {
        studio.run(blockSize);
        juce::Thread::sleep(1);
    }
    REQUIRE(studio.finishRecording());
    CHECK_THAT(session.takeRecordingProblem().toStdString(), ContainsSubstring("almost full"));
    CHECK(onlyClip(session, track));
}

TEST_CASE("Saving the project elsewhere takes its recordings along", "[controller][recording]")
{
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Moved");
    session.setTrackArmed(track, true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());
    REQUIRE(session.record().wasOk());
    studio.run(10000);
    session.stop();
    REQUIRE(studio.finishRecording());

    TemporaryDirectory elsewhere;
    REQUIRE(session.saveProject(elsewhere.file("copy.dawproject")).wasOk());

    const auto clip = onlyClip(session, track);
    REQUIRE(clip);
    CHECK(clip->getAudioFile() == elsewhere.file("Audio").getChildFile("Moved 1.wav"));
    CHECK(clip->getAudioFile().existsAsFile());
    CHECK(studio.directory.file("Audio").getChildFile("Moved 1.wav").existsAsFile());
    CHECK_THAT(elsewhere.file("copy.dawproject").loadFileAsString().toStdString(),
               ContainsSubstring("\"audioFile\": \"Audio/Moved 1.wav\""));
}

TEST_CASE("The recording latency is the device's plus the offset, never negative",
          "[controller][recording]")
{
    Studio studio;
    auto& session = studio.session;
    session.setDeviceLatency(100, 200);
    session.setLatencyOffset(-50);
    CHECK(session.getRecordingLatency() == 250);
    session.setLatencyOffset(-1000);
    CHECK(session.getRecordingLatency() == 0);
}

TEST_CASE("The journal lists unsaved recordings until the project is saved",
          "[controller][recording][recovery]")
{
    using daw::controller::RecordingJournal;

    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Journal");
    session.setTrackArmed(track, true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());

    session.seek(seconds(1.0));
    studio.run(blockSize);
    REQUIRE(session.record().wasOk());

    // Written before the first sample: the end is not known yet.
    auto entries = RecordingJournal::read(studio.directory.file(""));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].file == studio.directory.file("Audio").getChildFile("Journal 1.wav"));
    CHECK(entries[0].trackId == track);
    CHECK(entries[0].firstPosition == seconds(1.0));
    CHECK(entries[0].keepStart == seconds(1.0));
    CHECK_FALSE(entries[0].keepEnd);

    studio.run(48000);
    session.stop();
    REQUIRE(studio.finishRecording());
    entries = RecordingJournal::read(studio.directory.file(""));
    REQUIRE(entries.size() == 1);
    CHECK(entries[0].keepEnd == seconds(2.0));

    // A discarded recording leaves nothing in it.
    REQUIRE(session.record().wasOk());
    CHECK(RecordingJournal::read(studio.directory.file("")).size() == 2);
    session.discardRecording();
    REQUIRE(studio.finishRecording());
    CHECK(RecordingJournal::read(studio.directory.file("")).size() == 1);

    REQUIRE(session.saveProject(studio.projectFile()).wasOk());
    CHECK_FALSE(RecordingJournal::fileFor(studio.directory.file("")).exists());
}

TEST_CASE("A recording interrupted by a crash is recovered when the project is opened",
          "[controller][recording][recovery]")
{
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Vocals");
    session.setTrackArmed(track, true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());

    session.seek(seconds(1.0));
    studio.run(blockSize);
    const auto recordingStart = studio.device.getStreamPosition();
    REQUIRE(session.record().wasOk());
    studio.run(48000);
    juce::Thread::sleep(100); // the writer catches up

    // The crash: the project folder as it is on disk right now. The header of the file
    // being written says nothing yet, and its last frame is cut short.
    TemporaryDirectory crashed;
    REQUIRE(studio.directory.file("").copyDirectoryTo(crashed.file("")));
    const auto take = crashed.file("Audio").getChildFile("Vocals 1.wav");
    REQUIRE(take.getSize() > 3 * 10000);
    {
        juce::FileOutputStream stream(take);
        REQUIRE(stream.setPosition(take.getSize() - 1));
        REQUIRE(stream.truncate().wasOk());
    }

    session.discardRecording();
    REQUIRE(studio.finishRecording());

    daw::engine::Engine engine;
    engine.prepare(sampleRate, blockSize);
    Session reopened(engine);
    REQUIRE(reopened.openProject(crashed.file("song.dawproject")).wasOk());
    REQUIRE(reopened.getRecoverableRecordings().size() == 1);
    CHECK(reopened.getProject().getTrack(0).getNumClips() == 0);

    REQUIRE(reopened.recoverRecordings().wasOk());
    CHECK(reopened.getRecoverableRecordings().empty());
    CHECK(reopened.getUndoDescription() == "Recover Recordings");
    CHECK(reopened.hasUnsavedChanges());

    const auto clip = onlyClip(reopened, track);
    REQUIRE(clip);
    CHECK(clip->getAudioFile() == take);
    CHECK(clip->getPosition() == seconds(1.0));
    CHECK(clip->getOffset() == 0);
    // Every whole frame that reached the disk, exactly as it came in.
    const auto frames = daw::model::ticksToSamples(*clip->getLength(), sampleRate);
    CHECK(frames > 40000);
    CHECK(fileHolds(take, 1, frames, [&](int, juce::int64 k) {
        return testInputSample(0, recordingStart + k);
    }));
}

TEST_CASE("A recording made but not saved before a crash comes back as it was",
          "[controller][recording][recovery]")
{
    Studio studio;
    auto& session = studio.session;
    session.setLoopRange(seconds(0.5), seconds(1.5));
    session.setPunchEnabled(true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());
    const auto track = session.addTrack("New track"); // not in the saved project
    session.setTrackArmed(track, true);

    REQUIRE(session.record().wasOk());
    studio.run(2 * 48000);
    session.stop();
    REQUIRE(studio.finishRecording());
    const auto original = onlyClip(session, track);
    REQUIRE(original);

    TemporaryDirectory crashed;
    REQUIRE(studio.directory.file("").copyDirectoryTo(crashed.file("")));

    daw::engine::Engine engine;
    engine.prepare(sampleRate, blockSize);
    Session reopened(engine);
    REQUIRE(reopened.openProject(crashed.file("song.dawproject")).wasOk());
    REQUIRE(reopened.getRecoverableRecordings().size() == 1);
    REQUIRE(reopened.recoverRecordings().wasOk());

    // Its track was never saved: the clip goes on a new one, with the same bounds.
    REQUIRE(reopened.getProject().getNumTracks() == 1);
    const auto recovered = reopened.getProject().getTrack(0);
    CHECK(recovered.getName() == "New track 1");
    REQUIRE(recovered.getNumClips() == 1);
    CHECK(recovered.getClip(0).getBounds() == original->getBounds());
    CHECK(recovered.getClip(0).getBounds() ==
          daw::model::ClipBounds{seconds(0.5), seconds(0.5), seconds(1.0)});
}

TEST_CASE("Recoverable recordings can be discarded", "[controller][recording][recovery]")
{
    Studio studio;
    auto& session = studio.session;
    const auto track = session.addTrack("Gone");
    session.setTrackArmed(track, true);
    REQUIRE(session.saveProject(studio.projectFile()).wasOk());
    REQUIRE(session.record().wasOk());
    studio.run(20000);
    session.stop();
    REQUIRE(studio.finishRecording());

    TemporaryDirectory crashed;
    REQUIRE(studio.directory.file("").copyDirectoryTo(crashed.file("")));

    daw::engine::Engine engine;
    engine.prepare(sampleRate, blockSize);
    Session reopened(engine);
    REQUIRE(reopened.openProject(crashed.file("song.dawproject")).wasOk());
    REQUIRE(reopened.getRecoverableRecordings().size() == 1);
    reopened.discardRecoverableRecordings();

    CHECK_FALSE(crashed.file("Audio").getChildFile("Gone 1.wav").exists());
    CHECK_FALSE(daw::controller::RecordingJournal::fileFor(crashed.file("")).exists());
    CHECK(reopened.getProject().getTrack(0).getNumClips() == 0);

    // Opening the project again offers nothing.
    Session again(engine);
    REQUIRE(again.openProject(crashed.file("song.dawproject")).wasOk());
    CHECK(again.getRecoverableRecordings().empty());
}
