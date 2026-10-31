#include "daw/controller/ExportJob.h"
#include "daw/controller/Session.h"
#include "daw/model/ProjectFile.h"

#include "support/RealtimeCheck.h"
#include "support/SignalAnalysis.h"
#include "support/SimulatedDevice.h"
#include "support/TestAudioFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using daw::controller::ExportJob;
using daw::controller::ExportSettings;
using daw::controller::Session;
using daw::engine::Engine;
using daw::engine::TransportState;
using daw::model::ClipBounds;
using daw::model::Ticks;
using daw::model::ticksPerSecond;
using daw::test::AudioFileType;
using daw::test::TemporaryDirectory;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 512;
constexpr int waitMs = 30000;

juce::File writeConstant(const TemporaryDirectory& directory, const juce::String& name,
                         int numSamples, float value, double rate = sampleRate)
{
    const auto file = directory.file(name);
    juce::AudioBuffer<float> samples(2, numSamples);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(samples.getWritePointer(channel), value, numSamples);
    REQUIRE(daw::test::writeAudioFile(file, AudioFileType::wav, samples, rate, 32, true));
    return file;
}

// An engine as the device would run it, and a session driving it.
struct Fixture
{
    Fixture()
    {
        engine.prepare(sampleRate, blockSize);
    }

    // Runs the audio "device" for numBlocks and returns the left channel.
    std::vector<float> play(int numBlocks)
    {
        std::vector<float> left;
        juce::AudioBuffer<float> block(2, blockSize);
        for (int i = 0; i < numBlocks; ++i)
        {
            heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
            left.insert(left.end(), block.getReadPointer(0), block.getReadPointer(0) + blockSize);
        }
        return left;
    }

    Engine engine;
    Session session{engine};
    std::size_t heapOperations = 0;
};

} // namespace

TEST_CASE("Importing the same file for several clips loads it once", "[controller][session]")
{
    TemporaryDirectory directory;
    const auto file = writeConstant(directory, "loop.wav", 4800, 0.25f);

    Fixture fixture;
    auto& session = fixture.session;

    const auto first = session.importAudio({file}, 0);
    REQUIRE(first.size() == 1);
    const auto trackId = session.getProject().getTrack(0).getId();
    session.importAudio({file}, ticksPerSecond, trackId);
    session.importAudio({file}, 2 * ticksPerSecond, trackId);
    REQUIRE(session.waitForLoads(waitMs));

    // One track (named after the file), three clips, one load.
    CHECK(session.getProject().getNumTracks() == 1);
    CHECK(session.getProject().getTrack(0).getName() == "loop");
    CHECK(session.getProject().getTrack(0).getNumClips() == 3);
    const auto* source = session.findSource(file);
    REQUIRE(source != nullptr);
    CHECK(source->status == daw::engine::io::PooledSource::Status::ready);

    // Lengths learnt from the file, without counting as an undo step.
    const auto clip = session.getProject().getTrack(0).getClip(0);
    CHECK(clip.getLength() == ticksPerSecond / 10);
    CHECK(session.getFileLength(file) == ticksPerSecond / 10);
}

TEST_CASE("Several imported files go to one new track each", "[controller][session]")
{
    TemporaryDirectory directory;
    const auto a = writeConstant(directory, "drums.wav", 480, 0.1f);
    const auto b = writeConstant(directory, "bass.wav", 480, 0.2f);

    Fixture fixture;
    auto& session = fixture.session;
    const auto existing = session.addTrack();

    const auto clips = session.importAudio({a, b}, 5 * ticksPerSecond, existing);
    CHECK(clips.size() == 2);

    const auto& project = session.getProject();
    REQUIRE(project.getNumTracks() == 3);
    CHECK(project.getTrack(0).getNumClips() == 0);
    CHECK(project.getTrack(1).getName() == "drums");
    CHECK(project.getTrack(2).getName() == "bass");
    CHECK(project.getTrack(2).getClip(0).getPosition() == 5 * ticksPerSecond);

    // All of it is one undo step.
    CHECK(session.undo());
    CHECK(project.getNumTracks() == 1);
}

TEST_CASE("The engine follows the project after every change", "[controller][session]")
{
    TemporaryDirectory directory;
    const auto file = writeConstant(directory, "half.wav", 48000, 0.5f);

    Fixture fixture;
    auto& session = fixture.session;
    session.importAudio({file}, 0);
    REQUIRE(session.waitForLoads(waitMs));

    session.play();
    auto left = fixture.play(4);
    CHECK(left.back() == 0.5f);
    CHECK(fixture.engine.getLength() == 48000);

    // A volume change reaches the engine at the next update, as a ramp.
    const auto trackId = session.getProject().getTrack(0).getId();
    session.setTrackVolume(trackId, 0.5);
    session.update();
    left = fixture.play(4);
    CHECK(left.front() < 0.5f);
    CHECK(left.back() == 0.25f);

    // Undo brings the old volume back.
    REQUIRE(session.undo());
    session.update();
    left = fixture.play(4);
    CHECK(left.back() == 0.5f);

    // Muting.
    session.setTrackMuted(trackId, true);
    session.update();
    left = fixture.play(4);
    CHECK(left.back() == 0.0f);

    CHECK(fixture.heapOperations == 0);
}

TEST_CASE("A fader drag is one undo step", "[controller][session]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto trackId = session.addTrack();

    session.beginGesture("Volume");
    for (int i = 0; i < 50; ++i)
        session.setTrackVolume(trackId, 1.0 - i / 100.0);
    session.endGesture();

    CHECK(session.getProject().getTrack(0).getVolume() == 1.0 - 49 / 100.0);
    CHECK(session.getUndoDescription() == "Volume");
    REQUIRE(session.undo());
    CHECK(session.getProject().getTrack(0).getVolume() == 1.0);
    REQUIRE(session.undo()); // the track itself
    CHECK(session.getProject().getNumTracks() == 0);
    CHECK_FALSE(session.canUndo());
}

TEST_CASE("Undo and redo through the session restore the project exactly",
          "[controller][session][undo]")
{
    TemporaryDirectory directory;
    const auto file = writeConstant(directory, "a.wav", 48000, 0.5f);

    Fixture fixture;
    auto& session = fixture.session;
    std::vector<juce::String> states{daw::model::toJson(session.getProject(), {})};
    const auto record = [&] { states.push_back(daw::model::toJson(session.getProject(), {})); };

    const auto clips = session.importAudio({file}, 0);
    REQUIRE(session.waitForLoads(waitMs));
    record(); // with the length learnt from the file
    const auto drums = session.getProject().getTrack(0).getId();
    const auto bass = session.addTrack("Bass");
    record();
    session.moveClip(clips[0], bass, ticksPerSecond);
    record();
    session.setClipBounds(clips[0], {ticksPerSecond, ticksPerSecond / 10, ticksPerSecond / 2});
    record();
    session.setTrackPan(drums, -0.3);
    record();
    session.setTrackSoloed(bass, true);
    record();
    session.setTempo(97.0);
    record();
    session.setTimeSignature({7, 8});
    record();
    session.setLoopRange(ticksPerSecond, 3 * ticksPerSecond);
    record();
    session.setMasterVolume(0.7);
    record();
    session.setTrackInput(bass, {2, true});
    record();
    session.setTrackMonitoring(bass, daw::model::MonitoringMode::on);
    record();
    session.renameTrack(bass, "Bass 2");
    record();
    session.deleteClips({clips[0]});
    record();
    session.removeTrack(drums);
    record();

    // states[0] is the empty project, states[1] the state after the import.
    for (auto index = states.size() - 1; index > 1; --index)
    {
        REQUIRE(session.undo());
        REQUIRE(daw::model::toJson(session.getProject(), {}) == states[index - 1]);
    }
    for (std::size_t index = 2; index < states.size(); ++index)
    {
        REQUIRE(session.redo());
        REQUIRE(daw::model::toJson(session.getProject(), {}) == states[index]);
    }
}

TEST_CASE("Arming and recording settings are saved but not undoable", "[controller][session][undo]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto track = session.addTrack();
    REQUIRE(session.undo());
    REQUIRE(session.redo());
    const auto undoDescription = session.getUndoDescription();
    const auto revision = session.getRevision();

    session.setTrackArmed(track, true);
    session.setMetronomeEnabled(true);
    session.setMetronomeVolume(0.3);
    session.setCountInEnabled(true);
    session.setCountInBars(2);
    session.setPunchEnabled(true);
    session.setRecordingBitsPerSample(16);

    const auto& project = session.getProject();
    CHECK(project.getTrack(0).isArmed());
    CHECK(project.isMetronomeEnabled());
    CHECK(project.getMetronomeVolume() == 0.3);
    CHECK(project.isCountInEnabled());
    CHECK(project.getCountInBars() == 2);
    CHECK(project.isPunchEnabled());
    CHECK(project.getRecordingBitsPerSample() == 16);
    CHECK(session.hasUnsavedChanges());
    CHECK(session.getRevision() > revision);

    // Undo still goes back to the last edit, the new track, and leaves them alone.
    CHECK(session.getUndoDescription() == undoDescription);
    REQUIRE(session.undo());
    CHECK(project.getNumTracks() == 0);
    CHECK(project.isPunchEnabled());

    // Setting a track's input or monitoring to what it is adds no undo step.
    REQUIRE(session.redo());
    session.setTrackInput(track, {0, false});
    session.setTrackMonitoring(track, daw::model::MonitoringMode::automatic);
    CHECK(session.getUndoDescription() == undoDescription);
    session.setTrackMonitoring(track, daw::model::MonitoringMode::off);
    CHECK(session.getUndoDescription() == "Input Monitoring");
}

TEST_CASE("Track input meters show the levels of their input channels", "[controller][session]")
{
    daw::engine::Engine engine;
    daw::test::SimulatedDevice device(engine, sampleRate, 4, {blockSize});
    device.inputSignal = [](int channel, juce::int64) {
        return -0.1f * static_cast<float>(channel + 1);
    };
    device.start();

    daw::controller::Session session(engine);
    const auto mono = session.addTrack();
    const auto stereo = session.addTrack();
    const auto none = session.addTrack();
    session.setTrackInput(mono, {1, false});
    session.setTrackInput(stereo, {2, true});
    session.setTrackInput(none, daw::model::TrackInput::none());

    REQUIRE(device.run(2048) == 0);
    session.takeInputPeaks();

    CHECK(session.getInputPeak(0) == 0.1f);
    CHECK(session.getTrackInputPeaks(mono) == std::array<float, 2>{0.2f, 0.2f});
    CHECK(session.getTrackInputPeaks(stereo) == std::array<float, 2>{0.3f, 0.4f});
    CHECK(session.getTrackInputPeaks(none) == std::array<float, 2>{0.0f, 0.0f});

    // One reader: the peaks are taken once per refresh.
    session.takeInputPeaks();
    CHECK(session.getInputPeak(0) == 0.0f);
}

TEST_CASE("Changes are tracked until the project is saved", "[controller][session]")
{
    TemporaryDirectory directory;
    Fixture fixture;
    auto& session = fixture.session;

    CHECK_FALSE(session.hasUnsavedChanges());
    session.addTrack();
    CHECK(session.hasUnsavedChanges());

    const auto file = directory.file("song.dawproject");
    REQUIRE(session.saveProject(directory.file("song")).wasOk()); // extension added
    CHECK(session.getProjectFile() == file);
    CHECK_FALSE(session.hasUnsavedChanges());

    // Loop on/off is saved with the project, so it counts as a change too.
    session.setLoopEnabled(true);
    CHECK(session.hasUnsavedChanges());
    CHECK(session.getUndoDescription() == "Add Track");

    session.newProject();
    CHECK(session.getProject().getNumTracks() == 0);
    CHECK_FALSE(session.hasUnsavedChanges());
    CHECK_FALSE(session.canUndo());
    CHECK(session.getProjectFile() == juce::File());

    REQUIRE(session.openProject(file).wasOk());
    CHECK(session.getProject().getNumTracks() == 1);
    CHECK_FALSE(session.hasUnsavedChanges());

    const auto failed = session.openProject(directory.file("missing.dawproject"));
    CHECK(failed.failed());
    CHECK_THAT(failed.getErrorMessage().toStdString(), ContainsSubstring("does not exist"));
    CHECK(session.getProject().getNumTracks() == 1); // still the open one
}

TEST_CASE("A Phase 1 project opens and plays", "[controller][session]")
{
    TemporaryDirectory directory;
    REQUIRE(directory.file("audio").createDirectory().wasOk());
    writeConstant(directory, "audio/tone.wav", 24000, 0.5f);
    const auto file = directory.file("old.dawproject");
    REQUIRE(file.replaceWithText(R"({"format": "daw-project", "formatVersion": 1,
        "project": {"loopEnabled": true,
                    "tracks": [{"name": "Track 1",
                                "clips": [{"audioFile": "audio/tone.wav"}]}]}})"));

    Fixture fixture;
    auto& session = fixture.session;
    REQUIRE(session.openProject(file).wasOk());
    REQUIRE(session.waitForLoads(waitMs));

    // The whole file, looping, as in Phase 1.
    CHECK(session.getProject().getTrack(0).getClip(0).getLength() == ticksPerSecond / 2);
    CHECK_FALSE(session.hasUnsavedChanges());
    session.play();
    const auto left = fixture.play(100); // 51200 samples: more than two passes
    CHECK(fixture.engine.isLooping());
    CHECK(fixture.engine.getTransportState() == TransportState::playing);
    CHECK(left.back() == 0.5f);
}

TEST_CASE("Snapping follows the project settings and the zoom", "[controller][session]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const Ticks beat = ticksPerSecond / 2; // 120 BPM

    // Automatic grid at 1 beat = 20 pixels: quarter notes.
    const auto snap = session.makeSnapper(static_cast<double>(beat) / 20.0, false);
    CHECK(snap.isSnapping());
    CHECK(snap(beat + beat / 3) == beat);

    CHECK_FALSE(session.makeSnapper(1000.0, true).isSnapping()); // bypassed for one drag

    session.setGridDivision(daw::model::GridDivision::bar);
    CHECK(session.makeSnapper(1000.0, false)(beat * 3) == beat * 4);

    session.setSnapEnabled(false);
    CHECK_FALSE(session.makeSnapper(1000.0, false).isSnapping());
}

TEST_CASE("Seeking and the playhead are in ticks", "[controller][session]")
{
    TemporaryDirectory directory;
    const auto file = writeConstant(directory, "a.wav", 96000, 0.5f);

    Fixture fixture;
    auto& session = fixture.session;
    session.importAudio({file}, 0);
    REQUIRE(session.waitForLoads(waitMs));

    session.seek(ticksPerSecond / 2);
    fixture.play(1);
    CHECK(fixture.engine.getPlayheadPosition() == 24000);
    CHECK(session.getPlayhead() == ticksPerSecond / 2);
}

TEST_CASE("A new device rate reloads the files and keeps the times", "[controller][session]")
{
    TemporaryDirectory directory;
    const auto file = writeConstant(directory, "a.wav", 48000, 0.5f);

    Fixture fixture;
    auto& session = fixture.session;
    session.importAudio({file}, ticksPerSecond);
    REQUIRE(session.waitForLoads(waitMs));
    fixture.play(1);
    CHECK(fixture.engine.getLength() == 96000);

    fixture.engine.prepare(44100.0, blockSize);
    session.setSampleRate(44100.0);
    REQUIRE(session.waitForLoads(waitMs));
    fixture.play(1);
    CHECK(fixture.engine.getLength() == 88200);
    CHECK(session.getFileLength(file) == ticksPerSecond);
}

TEST_CASE("An export equals what the engine plays", "[controller][export]")
{
    TemporaryDirectory directory;
    const auto file = writeConstant(directory, "a.wav", 48000, 0.5f);

    Fixture fixture;
    auto& session = fixture.session;
    session.importAudio({file}, ticksPerSecond / 4);
    REQUIRE(session.waitForLoads(waitMs));
    session.setTrackVolume(session.getProject().getTrack(0).getId(), 0.5);

    ExportSettings settings;
    settings.file = directory.file("mix.wav");
    settings.sampleRate = sampleRate;
    settings.bitsPerSample = 32;
    ExportJob job(session, settings);
    const auto result = job.run();

    REQUIRE(result.result.wasOk());
    CHECK(result.samplesWritten == 60000); // to the end of the clip
    CHECK(result.peak == 0.25f);

    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(settings.file));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> written(2, 60000);
    REQUIRE(reader->read(&written, 0, 60000, 0, true, true));
    CHECK(written.getSample(0, 11999) == 0.0f);
    CHECK(written.getSample(0, 12000) == 0.25f);
    CHECK(written.getSample(1, 59999) == 0.25f);
}

TEST_CASE("An export at another rate converts from the original files", "[controller][export]")
{
    TemporaryDirectory directory;
    const auto file = writeConstant(directory, "a.wav", 44100, 0.5f, 44100.0);

    Fixture fixture; // the device runs at 48 kHz
    auto& session = fixture.session;
    session.importAudio({file}, 0);
    REQUIRE(session.waitForLoads(waitMs));
    session.setLoopRange(ticksPerSecond / 4, ticksPerSecond / 2);

    ExportSettings settings;
    settings.file = directory.file("mix.wav");
    settings.sampleRate = 44100.0;
    settings.bitsPerSample = 16;
    settings.range = ExportSettings::Range::loopRegion;

    std::vector<double> progress;
    ExportJob job(session, settings);
    const auto result = job.run([&progress](double done) {
        progress.push_back(done);
        return true;
    });

    REQUIRE(result.result.wasOk());
    CHECK(result.samplesWritten == 11025); // 0.25 s at 44.1 kHz
    CHECK(progress.back() == 1.0);

    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(settings.file));
    REQUIRE(reader != nullptr);
    CHECK(reader->sampleRate == 44100.0);
    CHECK(reader->bitsPerSample == 16);
    CHECK(reader->lengthInSamples == 11025);
}
