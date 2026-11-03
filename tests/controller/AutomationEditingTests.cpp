#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/AutomationEditing.h"
#include "daw/controller/ExportJob.h"
#include "daw/controller/Session.h"

#include "support/TestAudioFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <memory>

using Catch::Matchers::WithinAbs;
using daw::controller::ChainOwner;
using daw::controller::ExportJob;
using daw::controller::ExportSettings;
using daw::controller::Session;
using daw::model::AutomationPointData;
using daw::model::AutomationTarget;
using daw::model::ticksPerSecond;
using daw::test::TemporaryDirectory;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int waitMs = 30000;

struct Fixture
{
    Fixture() { engine.prepare(sampleRate, 512); }

    // The playhead moves once the engine has processed a block.
    void seek(daw::model::Ticks position)
    {
        session.seek(position);
        juce::AudioBuffer<float> block(2, 512);
        engine.process(block);
        session.update();
    }

    daw::engine::Engine engine;
    Session session{engine, {daw::engine::io::DiskStreamer::Mode::manual}};
};

juce::AudioBuffer<float> readFile(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));
    if (reader == nullptr)
        return {};
    juce::AudioBuffer<float> audio(2, static_cast<int>(reader->lengthInSamples));
    reader->read(&audio, 0, audio.getNumSamples(), 0, true, true);
    return audio;
}

} // namespace

TEST_CASE("Automation points are edited through the session, each edit one undo step",
          "[controller][automation]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto track = ChainOwner::track(session.addTrack("A"));
    const auto volume = AutomationTarget::volume();

    CHECK(session.addAutomationPoint(track, volume, {ticksPerSecond, 0.5, 0.0}) == 0);
    CHECK(session.addAutomationPoint(track, volume, {0, 0.25, 0.0}) == 0);
    CHECK(session.getUndoDescription() == "Add Automation Point");

    // A drag: many moves, one step.
    session.beginGesture("Move Automation Point");
    int index = 1;
    for (int step = 1; step <= 10; ++step)
        index = session.setAutomationPoint(track, volume, index,
                                           {ticksPerSecond + step * 1000, 0.5 + step * 0.01, 0.0});
    session.endGesture();
    const auto lane = session.getAutomation(track).getLane(0);
    CHECK(lane.getPoint(1).position == ticksPerSecond + 10000);
    CHECK_THAT(lane.getPoint(1).value, WithinAbs(0.6, 1e-6));
    CHECK(session.getUndoDescription() == "Move Automation Point");

    REQUIRE(session.undo());
    CHECK(session.getAutomation(track).getLane(0).getPoint(1).position == ticksPerSecond);
    REQUIRE(session.redo());

    session.removeAutomationPoint(track, volume, 0);
    CHECK(session.getAutomation(track).getLane(0).getNumPoints() == 1);
    session.clearAutomation(track, volume);
    CHECK(session.getAutomation(track).getNumLanes() == 0);
    REQUIRE(session.undo());
    CHECK(session.getAutomation(track).getLane(0).getNumPoints() == 1);

    // The mode is not an edit.
    const auto description = session.getUndoDescription();
    session.setAutomationMode(track, daw::model::AutomationMode::off);
    CHECK(session.getUndoDescription() == description);
    CHECK(session.getAutomation(track).getMode() == daw::model::AutomationMode::off);
}

TEST_CASE("Controls follow the automation at the playhead while it reads",
          "[controller][automation]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto track = ChainOwner::track(session.addTrack("A"));
    const auto pan = AutomationTarget::pan();
    CHECK_FALSE(session.getAutomatedValue(track, pan).has_value());

    session.addAutomationPoint(track, pan, {0, 0.0, 0.0});
    session.addAutomationPoint(track, pan, {ticksPerSecond, 1.0, 0.0});
    fixture.seek(ticksPerSecond / 4);
    REQUIRE(session.getAutomatedValue(track, pan).has_value());
    CHECK_THAT(*session.getAutomatedValue(track, pan), WithinAbs(0.25, 1e-6));

    session.setAutomationMode(track, daw::model::AutomationMode::off);
    CHECK_FALSE(session.getAutomatedValue(track, pan).has_value());
}

TEST_CASE("The value a lane shows is the value the engine plays", "[controller][automation]")
{
    daw::model::Project project;
    auto lane = project.addTrack("A").getAutomation().getOrAddLane(AutomationTarget::volume());
    lane.addPoint({0, 0.1, 0.0});
    lane.addPoint({ticksPerSecond, 0.9, 0.7});
    lane.addPoint({ticksPerSecond * 3, 0.2, -0.4});
    lane.addPoint({ticksPerSecond * 3, 0.6, 0.0});

    const auto curve = daw::controller::makeCurve(lane, sampleRate);
    REQUIRE(curve != nullptr);
    bool same = true;
    for (juce::int64 sample = 0; sample < 4 * 48000; sample += 37)
    {
        const auto ticks = daw::model::samplesToTicks(sample, sampleRate);
        same = same && static_cast<float>(*daw::controller::automationValueAt(lane, ticks)) ==
                           curve->valueAt(sample);
    }
    CHECK(same);
}

TEST_CASE("Finding points and segments, and bending segments by dragging",
          "[controller][automation]")
{
    daw::model::Project project;
    auto lane = project.addTrack("A").getAutomation().getOrAddLane(AutomationTarget::volume());
    lane.addPoint({1000, 0.2, 0.0});
    lane.addPoint({2000, 0.8, 0.0});
    lane.addPoint({3000, 0.4, 0.0});

    CHECK(daw::controller::findAutomationPoint(lane, 1990, 0.79, 50, 0.05) == 1);
    CHECK(daw::controller::findAutomationPoint(lane, 1900, 0.79, 50, 0.05) == -1);
    CHECK(daw::controller::findAutomationPoint(lane, 2000, 0.5, 50, 0.05) == -1);
    CHECK(daw::controller::automationSegmentAt(lane, 500) == -1);
    CHECK(daw::controller::automationSegmentAt(lane, 1500) == 0);
    CHECK(daw::controller::automationSegmentAt(lane, 2000) == 1);
    CHECK(daw::controller::automationSegmentAt(lane, 3000) == -1);

    // Rising segment dragged down: it sags (positive bend); falling dragged up: too.
    CHECK(daw::controller::bendForDrag(0.0, 0.2, 0.8, -0.1) > 0.0);
    CHECK(daw::controller::bendForDrag(0.0, 0.8, 0.4, 0.1) > 0.0);
    CHECK(daw::controller::bendForDrag(0.9, 0.2, 0.8, -1.0) == 1.0);
}

TEST_CASE("An export can include the effects' tails, and has nothing more without them",
          "[controller][export]")
{
    TemporaryDirectory directory;
    const auto file = directory.file("short.wav");
    juce::AudioBuffer<float> samples(2, 4800); // 0.1 s
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(samples.getWritePointer(channel), 0.5f, 4800);
    REQUIRE(daw::test::writeAudioFile(file, daw::test::AudioFileType::wav, samples, sampleRate, 32, true));

    Fixture fixture;
    auto& session = fixture.session;
    session.importAudio({file}, 0);
    REQUIRE(session.waitForLoads(waitMs));
    const auto track = session.getProject().getTrack(0).getId();

    const auto exportTo = [&](const juce::String& name, bool tail) {
        ExportSettings settings;
        settings.file = directory.file(name);
        settings.sampleRate = sampleRate;
        settings.bitsPerSample = 32;
        settings.includeTail = tail;
        ExportJob job(session, settings);
        return job.run();
    };

    // No effects: the tail adds nothing.
    CHECK(exportTo("plain.wav", true).samplesWritten == 4800);

    // A delay with feedback: echoes every 100 ms, each half as loud, down to -96 dBFS
    // after about 16 echoes.
    const auto delay = session.addEffect(ChainOwner::track(track), "delay");
    session.setEffectParameter(delay, "time", 100.0);
    session.setEffectParameter(delay, "feedback", 50.0);
    session.setEffectParameter(delay, "mix", 50.0);

    const auto withTail = exportTo("tail.wav", true);
    const auto withoutTail = exportTo("cut.wav", false);
    REQUIRE(withTail.result.wasOk());
    CHECK(withoutTail.samplesWritten == 4800);
    CHECK(withTail.samplesWritten > 48000);
    CHECK(withTail.samplesWritten < 3 * 48000);

    const auto tail = readFile(directory.file("tail.wav"));
    const auto cut = readFile(directory.file("cut.wav"));
    REQUIRE(tail.getNumSamples() == withTail.samplesWritten);
    bool samePart = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 4800; ++i)
            samePart = samePart && tail.getSample(channel, i) == cut.getSample(channel, i);
    CHECK(samePart);
    CHECK(tail.getMagnitude(0, 4800 * 2, 4800) > 0.05f);                   // the echoes
    CHECK(tail.getMagnitude(0, tail.getNumSamples() - 100, 100) < 1e-3f); // dying away
}
