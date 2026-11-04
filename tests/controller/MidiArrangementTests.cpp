#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/ExportJob.h"
#include "daw/controller/Session.h"

#include "support/InstrumentRunner.h"
#include "support/RealtimeCheck.h"
#include "support/TestAudioFiles.h"
#include "support/TestEffects.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <map>
#include <vector>

namespace midi = daw::engine::midi;
using daw::controller::buildArrangement;
using daw::controller::noteIdentity;
using daw::controller::Session;
using daw::controller::SessionOptions;
using daw::engine::ArrangementNote;
using daw::engine::Engine;
using daw::model::ControllerType;
using daw::model::Note;
using daw::model::Project;
using daw::model::Ticks;
using daw::model::TrackKind;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr Ticks sample = 5880; // ticks per sample at 48 kHz
constexpr int blockSize = 256;

// The test instrument for type "test-instrument"; the instances made last, by type.
std::unique_ptr<daw::engine::fx::Effect> makeTestEffect(const juce::String& type)
{
    if (type == "test-instrument")
        return std::make_unique<daw::test::TestInstrument>();
    return nullptr;
}

SessionOptions testOptions()
{
    SessionOptions options;
    options.streaming = daw::engine::io::DiskStreamer::Mode::manual;
    options.effects = makeTestEffect;
    return options;
}

struct Fixture
{
    Fixture()
    {
        engine.prepare(sampleRate, blockSize);
        session.setSampleRate(sampleRate);
    }

    std::vector<float> play(int numBlocks)
    {
        std::vector<float> left;
        juce::AudioBuffer<float> block(2, blockSize);
        for (int i = 0; i < numBlocks; ++i)
        {
            heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
            left.insert(left.end(), block.getReadPointer(0), block.getReadPointer(0) + blockSize);
            session.update();
        }
        return left;
    }

    daw::test::TestInstrument& instrumentOf(daw::model::ObjectId trackId)
    {
        const auto track = session.getProject().getTrack(session.getProject().indexOfTrack(trackId));
        auto instance = session.getEffectInstance(track.getInstrument()->getId());
        REQUIRE(instance != nullptr);
        return dynamic_cast<daw::test::TestInstrument&>(instance->getEffect());
    }

    Engine engine;
    Session session{engine, testOptions()};
    std::size_t heapOperations = 0;
};

} // namespace

TEST_CASE("MIDI clips become notes in samples, the clip on top covering the ones below",
          "[controller][midi]")
{
    Project project;
    auto track = project.addTrack("Keys", TrackKind::midi);
    track.setMidiChannel(2);

    // Clip A shows its content from 500 samples on, over [0, 4000); clip B covers
    // [2000, 3000) on top of it.
    const auto a = track.addMidiClip("A", {0, 500 * sample, 4000 * sample},
                                     {{1, 400 * sample, 1000 * sample, 60, 100},  // before the offset
                                      {2, 600 * sample, 1000 * sample, 62, 127},  // [100, 1100)
                                      {3, 2000 * sample, 1000 * sample, 64, 64},  // cut by B at 2000
                                      {4, 2700 * sample, 100 * sample, 65, 64},   // under B
                                      {5, 3600 * sample, 2000 * sample, 67, 1}}); // cut at A's end
    const auto b = track.addMidiClip("B", {2000 * sample, 0, 1000 * sample},
                                     {{1, 0, 5000 * sample, 48, 90},   // cut at B's end
                                      {2, 999 * sample, 1, 50, 90}}); // shorter than a sample

    const auto arrangement = buildArrangement(project, sampleRate, {});
    REQUIRE(arrangement->tracks.size() == 1);
    const auto& played = arrangement->tracks[0];
    CHECK(played.playsNotes);
    CHECK(arrangement->length == 4000);

    const auto note = [](juce::int64 start, juce::int64 end, int key, int velocity,
                         daw::model::ObjectId clip, std::int32_t id) {
        return ArrangementNote{start,
                               end,
                               static_cast<std::uint8_t>(key),
                               1,
                               midi::fromSevenBit(velocity),
                               noteIdentity(clip, id)};
    };
    const std::vector<ArrangementNote> expected{
        note(100, 1100, 62, 127, a.getId(), 2), note(1500, 2000, 64, 64, a.getId(), 3),
        note(2000, 3000, 48, 90, b.getId(), 1), note(2999, 3000, 50, 90, b.getId(), 2),
        note(3100, 4000, 67, 1, a.getId(), 5)};
    CHECK(played.notes == expected);
}

TEST_CASE("Controller lanes step through the visible parts and rest after each clip",
          "[controller][midi]")
{
    Project project;
    auto track = project.addTrack("Keys", TrackKind::midi);
    const auto modWheel = ControllerType::controlChange(1);
    track.addMidiClip("A", {0, 500 * sample, 4000 * sample}, {},
                      {{modWheel, 500 * sample, 10}, {modWheel, 1500 * sample, 50}});
    track.addMidiClip("B", {2000 * sample, 0, 1000 * sample}, {},
                      {{modWheel, 0, 100}, {ControllerType::pitchBend(), 500 * sample, 12000}});

    const auto arrangement = buildArrangement(project, sampleRate, {});
    const auto& lanes = arrangement->tracks[0].controllers;
    REQUIRE(lanes.size() == 2);

    std::map<midi::EventType, daw::engine::ControllerLane> byType;
    for (const auto& lane : lanes)
        byType[lane.type] = lane;

    const auto& wheel = byType[midi::EventType::controller];
    CHECK(wheel.number == 1);
    const std::vector<daw::engine::ControllerStep> wheelSteps{
        {0, midi::fromSevenBit(10)},    {1000, midi::fromSevenBit(50)},
        {2000, midi::fromSevenBit(100)}, {3000, midi::fromSevenBit(50)},
        {4000, 0.0f}};
    CHECK(wheel.steps == wheelSteps);

    const auto& bend = byType[midi::EventType::pitchBend];
    const std::vector<daw::engine::ControllerStep> bendSteps{{2500, midi::fromPitchBend(12000)},
                                                             {3000, 0.0f}};
    CHECK(bend.steps == bendSteps);
}

TEST_CASE("A MIDI track not heard plays nothing; its input is found by device name",
          "[controller][midi]")
{
    Project project;
    auto keys = project.addTrack("Keys", TrackKind::midi);
    keys.addMidiClip("A", {0, 0, 1000 * sample}, {{1, 0, 100 * sample, 60, 100}});
    keys.setMidiInput({"Keystation", 3});
    keys.setMuted(true);
    auto other = project.addTrack("Other", TrackKind::midi);
    other.setMidiInput({"Unplugged", 0});

    const auto devices = [](const juce::String& name) { return name == "Keystation" ? 4 : -1; };
    const auto arrangement = buildArrangement(project, sampleRate, {}, {}, {}, devices);
    CHECK(arrangement->tracks[0].notes.empty());
    CHECK(arrangement->length == 1000);
    CHECK(arrangement->tracks[0].midiInput == midi::InputFilter{4, 2});
    CHECK(arrangement->tracks[1].midiInput.device == midi::InputFilter::noDevice);
    CHECK(arrangement->tracks[1].midiInput.channel == -1);

    // Without a lookup, every device.
    CHECK(buildArrangement(project, sampleRate, {})->tracks[0].midiInput.device ==
          midi::InputFilter::everyDevice);
}

TEST_CASE("A MIDI clip plays through the session on its exact samples", "[controller][midi]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto track = session.addMidiTrack("Keys", "test-instrument");
    const auto clip = session.addMidiClip(track, 1000 * sample, 4000 * sample);
    session.setNotes(clip, {{0, 0, 300 * sample, 60, 127}, {0, 3900 * sample, 500 * sample, 62, 127}},
                     "Notes");
    session.update();
    session.play();

    const auto left = fixture.play(24);
    CHECK(fixture.heapOperations == 0);
    for (std::size_t i = 0; i < 6000; ++i)
    {
        const bool sounds = (i >= 1000 && i < 1300) || (i >= 4900 && i < 5000);
        CAPTURE(i);
        REQUIRE(left[i] == (sounds ? 1.0f : 0.0f));
    }
    CHECK(fixture.instrumentOf(track).wellFormed);
}

TEST_CASE("Editing a MIDI track while it plays leaves no note hanging", "[controller][midi]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto track = session.addMidiTrack("Keys", "test-instrument");
    const auto clip = session.addMidiClip(track, 0, 48000 * sample);
    session.setNotes(clip, {{0, 0, 40000 * sample, 60, 127}, {0, 0, 40000 * sample, 64, 127}},
                     "Notes");
    session.update();
    session.play();
    fixture.play(8);
    // Kept alive here: the session lets go of an instrument that goes.
    const auto firstInstance = session.getEffectInstance(
        session.getProject().getTrack(0).getInstrument()->getId());
    auto* first = &fixture.instrumentOf(track);
    REQUIRE(first->getHeldCount() == 2);

    SECTION("deleting the clip")
    {
        session.deleteClips({clip});
        fixture.play(8);
        CHECK(first->getHeldCount() == 0);
    }
    SECTION("muting the track")
    {
        session.setTrackMuted(track, true);
        fixture.play(8);
        CHECK(first->getHeldCount() == 0);
    }
    SECTION("bypassing the instrument")
    {
        session.setEffectBypassed(session.getProject().getTrack(0).getInstrument()->getId(), true);
        fixture.play(8);
        CHECK(first->getHeldCount() == 0);
    }
    SECTION("replacing the instrument")
    {
        REQUIRE(session.setInstrument(track, "test-instrument") != 0);
        fixture.play(8);
        CHECK(first->getHeldCount() == 0);
        CHECK(first->wellFormed);
        // The new one plays the next notes (not the ones that were already sounding).
        CHECK(fixture.instrumentOf(track).getHeldCount() == 0);
    }
    SECTION("removing the track")
    {
        session.removeTrack(track);
        fixture.play(8);
        CHECK(first->getHeldCount() == 0);
    }
    CHECK(first->wellFormed);
    CHECK(fixture.heapOperations == 0);
}

TEST_CASE("Previews play a note through the track's instrument", "[controller][midi]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto track = session.addMidiTrack("Keys", "test-instrument");
    session.update();
    fixture.play(2);

    session.startNotePreview(track, 72, 64);
    const auto on = fixture.play(2);
    CHECK(on.front() == midi::fromSevenBit(64));
    CHECK(fixture.instrumentOf(track).getHeldCount() == 1);
    session.stopNotePreview(track, 72);
    const auto off = fixture.play(2);
    CHECK(off.front() == 0.0f);
    CHECK(fixture.instrumentOf(track).getHeldCount() == 0);
}

TEST_CASE("A sampler plays its file through the session, and the export holds the same",
          "[controller][midi][sampler]")
{
    daw::test::TemporaryDirectory directory;
    const auto file = directory.file("tone.wav");
    {
        // A 1 s tone at 441 Hz, written at 44.1 kHz: loaded at the device's 48 kHz.
        juce::AudioBuffer<float> tone(1, 44100);
        for (int i = 0; i < 44100; ++i)
            tone.setSample(0, i, 0.5f * static_cast<float>(std::sin(2.0 * 3.141592653589793 * 441.0 * i / 44100.0)));
        REQUIRE(daw::test::writeAudioFile(file, daw::test::AudioFileType::wav, tone, 44100.0, 32, true));
    }

    Fixture fixture;
    auto& session = fixture.session;
    const auto track = session.addMidiTrack("Piano", "sampler");
    const auto sampler = session.getProject().getTrack(0).getInstrument()->getId();
    session.setEffectParameter(sampler, "velocity", 0.0);
    session.setInstrumentFile(sampler, "sample", file);
    CHECK(session.getUndoDescription() == "Load Sample");
    const auto clip = session.addMidiClip(track, 0, 48000 * sample);
    session.setNotes(clip, {{0, 0, 24000 * sample, 72, 100}}, "Note"); // an octave up
    REQUIRE(session.waitForLoads(30000));
    CHECK(session.getSamples().getStatus(file) == daw::controller::SampleCache::Status::ready);
    session.play();
    const auto played = fixture.play(96);
    CHECK(fixture.heapOperations == 0);
    CHECK(daw::test::measuredFrequency(played, 1000, 23000, sampleRate) ==
          Catch::Approx(882.0).epsilon(1e-5));

    daw::test::TemporaryDirectory exports;
    daw::controller::ExportSettings settings;
    settings.file = exports.file("mix.wav");
    settings.sampleRate = sampleRate;
    settings.bitsPerSample = 32;
    settings.includeTail = false;
    const auto result = daw::controller::ExportJob(session, settings).run();
    REQUIRE(result.result.wasOk());
    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(settings.file));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> exported(2, static_cast<int>(reader->lengthInSamples));
    reader->read(&exported, 0, exported.getNumSamples(), 0, true, true);
    // After the transport's fade-in... instruments are not faded: equal from the start.
    bool identical = true;
    for (int i = 0; i < 24000; ++i)
        identical = identical && exported.getSample(0, i) == played[static_cast<std::size_t>(i)];
    CHECK(identical);
}
