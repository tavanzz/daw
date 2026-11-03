#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/FadeOutKeeper.h"
#include "daw/controller/Session.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/dsp/PanLaw.h"
#include "daw/engine/fx/Equaliser.h"

#include "support/SimulatedDevice.h"
#include "support/TestAudioFiles.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <memory>

using Catch::Matchers::ContainsSubstring;
using daw::controller::buildArrangement;
using daw::controller::ChainOwner;
using daw::controller::FadeOutKeeper;
using daw::controller::Session;
using daw::model::AutomationTarget;
using daw::model::ObjectId;
using daw::model::Project;
using daw::model::ticksPerSecond;

namespace
{

constexpr double sampleRate = 48000.0;

std::unique_ptr<daw::engine::Arrangement> build(const Project& project)
{
    return buildArrangement(project, sampleRate, {});
}

float normalised(const daw::engine::ChainSlot& slot, const juce::String& id, float value)
{
    const auto& description = slot.instance->getDescription();
    return description.parameters[static_cast<std::size_t>(description.indexOf(id))].toNormalised(value);
}

} // namespace

TEST_CASE("Effects, buses and sends reach the arrangement as the project says",
          "[controller][fx]")
{
    Project project;
    auto track = project.addTrack("A");
    track.setVolume(0.5);
    auto other = project.addTrack("B");
    auto bus = project.addBus("Reverb");
    bus.setVolume(0.8);
    bus.setPan(-1.0);
    bus.getEffects().add("reverb", -1, {{"mix", 100.0}});
    track.getEffects().add("equaliser", -1, {{"bell1Gain", 6.0}});
    track.getEffects().add("compressor", -1).setBypassed(true);
    track.getEffects().add("unknown-plugin", -1);
    auto send = track.addSend(bus.getId());
    send.setLevel(0.5);
    send.setPreFader(true);
    project.getMasterEffects().add("limiter", -1);
    project.setTempo(90.0);

    auto arrangement = build(project);
    REQUIRE(arrangement->tracks.size() == 2);
    const auto& built = arrangement->tracks[0];

    // Unknown types are left out; values are normalised; bypass switches off.
    REQUIRE(built.inserts.slots.size() == 2);
    CHECK(built.inserts.slots[0].instance->getDescription().type == "equaliser");
    const auto gainIndex = static_cast<std::size_t>(
        built.inserts.slots[0].instance->getDescription().indexOf("bell1Gain"));
    CHECK(built.inserts.slots[0].parameters[gainIndex] ==
          normalised(built.inserts.slots[0], "bell1Gain", 6.0f));
    CHECK(built.inserts.slots[0].enabled);
    CHECK_FALSE(built.inserts.slots[1].enabled);

    REQUIRE(built.sends.size() == 1);
    CHECK(built.sends[0].gain == 0.5f);
    CHECK(built.sends[0].preFader);
    CHECK(built.sends[0].busIndex == 0);
    CHECK(built.sends[0].bus == bus.getId());

    REQUIRE(arrangement->buses.size() == 1);
    const auto pan = daw::engine::dsp::panGains(-1.0);
    CHECK(arrangement->buses[0].leftGain == static_cast<float>(0.8 * pan.left));
    CHECK(arrangement->buses[0].rightGain == 0.0f);
    CHECK(arrangement->buses[0].inserts.slots.size() == 1);
    CHECK(arrangement->masterInserts.slots.size() == 1);
    CHECK(arrangement->tempo == 90.0);

    // The limiter on the master delays the output by its lookahead.
    CHECK(daw::engine::MixState::startingAt(*arrangement).latency == 72);

    // Not heard (muted, or another track soloed): no sends. A muted bus is silent.
    track.setMuted(true);
    CHECK(build(project)->tracks[0].sends[0].gain == 0.0f);
    track.setMuted(false);
    other.setSoloed(true);
    CHECK(build(project)->tracks[0].sends[0].gain == 0.0f);
    other.setSoloed(false);
    bus.setMuted(true);
    CHECK(build(project)->buses[0].leftGain == 0.0f);
}

TEST_CASE("Automation that reads becomes curves in samples; off, it is ignored",
          "[controller][fx][automation]")
{
    Project project;
    auto track = project.addTrack("A");
    auto bus = project.addBus("Bus");
    const auto eq = track.getEffects().add("equaliser", -1);
    const auto send = track.addSend(bus.getId());
    auto automation = track.getAutomation();
    automation.getOrAddLane(AutomationTarget::volume()).addPoint({ticksPerSecond, 0.25, 0.5});
    automation.getOrAddLane(AutomationTarget::volume()).addPoint({ticksPerSecond * 2, 0.75, 0.0});
    automation.getOrAddLane(AutomationTarget::pan()).addPoint({0, 1.0, 0.0});
    automation.getOrAddLane(AutomationTarget::sendLevel(send.getId())).addPoint({0, 0.5, 0.0});
    automation.getOrAddLane(AutomationTarget::effectParameter(eq.getId(), "bell2Gain"))
        .addPoint({ticksPerSecond / 2, 0.9, 0.0});
    automation.getOrAddLane(AutomationTarget::effectParameter(eq.getId(), "bell3Gain")); // empty
    project.getMasterAutomation().getOrAddLane(AutomationTarget::volume()).addPoint({0, 0.6, 0.0});

    auto arrangement = build(project);
    const auto& built = arrangement->tracks[0];
    REQUIRE(built.automation.volumeCurve != nullptr);
    const auto& points = built.automation.volumeCurve->getPoints();
    REQUIRE(points.size() == 2);
    CHECK(points[0].position == 48000);
    CHECK(points[0].value == 0.25f);
    CHECK(points[0].curve == 0.5f);
    CHECK(points[1].position == 96000);
    CHECK(built.automation.panCurve != nullptr);
    CHECK(built.sends[0].levelCurve != nullptr);
    CHECK(arrangement->masterCurve != nullptr);

    const auto& slot = built.inserts.slots[0];
    const auto& description = slot.instance->getDescription();
    REQUIRE(slot.automation.size() == description.parameters.size());
    const auto bell2 = static_cast<std::size_t>(description.indexOf("bell2Gain"));
    const auto bell3 = static_cast<std::size_t>(description.indexOf("bell3Gain"));
    REQUIRE(slot.automation[bell2] != nullptr);
    CHECK(slot.automation[bell2]->getPoints()[0].position == 24000);
    CHECK(slot.automation[bell3] == nullptr); // no points: no automation

    // Off: everything as set, no curves.
    automation.setMode(daw::model::AutomationMode::off);
    arrangement = build(project);
    CHECK(arrangement->tracks[0].automation.volumeCurve == nullptr);
    CHECK(arrangement->tracks[0].sends[0].levelCurve == nullptr);
    CHECK(arrangement->tracks[0].inserts.slots[0].automation.empty());

    // A muted track reads no automation of what is not heard.
    automation.setMode(daw::model::AutomationMode::read);
    track.setMuted(true);
    arrangement = build(project);
    CHECK(arrangement->tracks[0].automation.volumeCurve == nullptr);
    CHECK(arrangement->tracks[0].sends[0].levelCurve == nullptr);
}

TEST_CASE("Removed effects, sends and buses are kept silent for a moment, then go",
          "[controller][fx]")
{
    const auto host = [] {
        daw::engine::fx::ProcessSetup setup;
        return std::make_shared<daw::engine::fx::EffectInstance>(
            std::make_unique<daw::test::GainEffect>(), setup);
    };
    const auto slot = [](std::shared_ptr<daw::engine::fx::EffectInstance> instance) {
        daw::engine::ChainSlot chainSlot;
        chainSlot.instance = std::move(instance);
        return chainSlot;
    };

    daw::engine::Arrangement before;
    daw::engine::ArrangementTrack track;
    track.id = 1;
    const auto a = host();
    const auto b = host();
    const auto c = host();
    track.inserts.slots = {slot(a), slot(b), slot(c)};
    daw::engine::ArrangementSend send;
    send.id = 5;
    send.bus = 7;
    send.busIndex = 0;
    send.gain = 0.5f;
    track.sends = {send};
    before.tracks = {track};
    daw::engine::ArrangementBus bus;
    bus.id = 7;
    before.buses = {bus};

    // B, the send and the bus are gone from the project.
    const auto rebuilt = [&] {
        daw::engine::Arrangement now;
        auto kept = track;
        kept.inserts.slots = {slot(a), slot(c)};
        kept.sends.clear();
        now.tracks = {kept};
        return now;
    };

    FadeOutKeeper keeper;
    auto now = rebuilt();
    keeper.keep(now, &before, 10.0);
    REQUIRE(now.tracks[0].inserts.slots.size() == 3);
    CHECK(now.tracks[0].inserts.slots[1].instance == b);
    CHECK_FALSE(now.tracks[0].inserts.slots[1].enabled);
    REQUIRE(now.tracks[0].sends.size() == 1);
    CHECK(now.tracks[0].sends[0].gain == 0.0f);
    REQUIRE(now.buses.size() == 1);
    CHECK(now.buses[0].leftGain == 0.0f);
    CHECK(now.tracks[0].sends[0].busIndex == 0);
    CHECK_FALSE(keeper.isDue(10.1));
    CHECK(keeper.isDue(10.0 + FadeOutKeeper::keepSeconds));

    // Rebuilt again later (the engine has the arrangement with them kept): gone.
    auto later = rebuilt();
    keeper.keep(later, &now, 10.0 + FadeOutKeeper::keepSeconds);
    CHECK(later.tracks[0].inserts.slots.size() == 2);
    CHECK(later.tracks[0].sends.empty());
    CHECK(later.buses.empty());
    CHECK_FALSE(keeper.isDue(100.0));
}

TEST_CASE("Effect instances last across edits; a moved effect starts anew where it went",
          "[controller][fx]")
{
    daw::engine::Engine engine;
    engine.prepare(sampleRate, 512);
    Session session(engine);
    const auto track = session.addTrack("A");
    const auto eq = session.addEffect(ChainOwner::track(track), "equaliser");
    const auto compressor = session.addEffect(ChainOwner::track(track), "compressor");
    CHECK(session.addEffect(ChainOwner::track(track), "no-such-effect") == 0);
    CHECK(session.addEffect(ChainOwner::track(12345), "equaliser") == 0);
    session.update();

    const auto eqInstance = session.getEffectInstance(eq);
    const auto compressorInstance = session.getEffectInstance(compressor);
    REQUIRE(eqInstance != nullptr);
    REQUIRE(compressorInstance != nullptr);

    session.setEffectParameter(eq, "bell1Gain", 3.0);
    session.setEffectBypassed(compressor, true);
    session.update();
    CHECK(session.getEffectInstance(eq) == eqInstance);
    CHECK(session.getEffectInstance(compressor) == compressorInstance);
    CHECK(session.getEffect(eq)->getParameter("bell1Gain") == 3.0);

    // Moving the compressor first: the compressor gets a new instance (the old one
    // fades out where it was), the equaliser keeps its own.
    REQUIRE(session.moveEffect(compressor, 0));
    session.update();
    CHECK(session.getEffectInstance(compressor) != compressorInstance);
    CHECK(session.getEffectInstance(eq) == eqInstance);
    CHECK(session.findEffect(compressor)->index == 0);

    // Undo puts it back; removal and its undo.
    REQUIRE(session.undo());
    CHECK(session.findEffect(compressor)->index == 1);
    REQUIRE(session.removeEffect(eq));
    session.update();
    CHECK(session.getEffectInstance(eq) == nullptr);
    REQUIRE(session.undo());
    session.update();
    CHECK(session.getEffectInstance(eq) != nullptr);
    CHECK(session.getRedoDescription() == "Remove Effect");
}

TEST_CASE("Buses and sends edited through the session, with undo", "[controller][fx]")
{
    daw::engine::Engine engine;
    engine.prepare(sampleRate, 512);
    Session session(engine);
    const auto track = session.addTrack("A");
    const auto bus = session.addBus();
    CHECK(session.getProject().getBus(0).getName() == "Bus 1");

    // Delay and reverb are fully wet on a bus, and keep their default elsewhere.
    const auto reverb = session.addEffect(ChainOwner::bus(bus), "reverb");
    CHECK(session.getEffect(reverb)->getParameter("mix") == 100.0);
    const auto trackDelay = session.addEffect(ChainOwner::track(track), "delay");
    CHECK_FALSE(session.getEffect(trackDelay)->getParameter("mix").has_value());

    const auto send = session.addSend(track, bus);
    CHECK(session.addSend(track, bus) == send);
    session.setSendLevel(track, send, 0.25);
    session.setSendPreFader(track, send, true);
    session.setBusVolume(bus, 0.5);
    session.setBusPan(bus, 0.5);
    session.setBusMuted(bus, true);
    session.renameBus(bus, "Room");
    const auto sendState = session.getProject().getTrack(0).getSend(0);
    CHECK(sendState.getLevel() == 0.25);
    CHECK(sendState.isPreFader());
    CHECK(session.getProject().getBus(0).getName() == "Room");
    CHECK(session.getBusMeter(bus) != nullptr);

    session.removeBus(bus);
    CHECK(session.getProject().getNumBuses() == 0);
    CHECK(session.getProject().getTrack(0).getNumSends() == 0);
    REQUIRE(session.undo()); // the bus and its send come back together
    CHECK(session.getProject().getNumBuses() == 1);
    CHECK(session.getProject().getTrack(0).getNumSends() == 1);
}

TEST_CASE("Effects that delay the audio cannot be added or removed while recording",
          "[controller][fx][recording]")
{
    daw::engine::Engine engine;
    daw::test::SimulatedDevice device(engine, sampleRate, 2, {512});
    device.start();
    Session session(engine);
    daw::test::TemporaryDirectory directory;

    const auto track = session.addTrack("Mic");
    const auto limiter = session.addEffect(ChainOwner::master(), "limiter");
    REQUIRE(limiter != 0);
    session.setTrackArmed(track, true);
    REQUIRE(session.saveProject(directory.file("song.dawproject")).wasOk());
    REQUIRE(session.record().wasOk());
    (void) device.run(4096);
    session.update();
    REQUIRE(session.isRecording());

    CHECK(session.addEffect(ChainOwner::master(), "limiter") == 0);
    CHECK_THAT(session.takeEditProblem().toStdString(), ContainsSubstring("while recording"));
    CHECK_FALSE(session.removeEffect(limiter));
    CHECK(session.takeEditProblem().isNotEmpty());
    CHECK(session.addEffect(ChainOwner::track(track), "equaliser") != 0); // no latency: fine
    session.setEffectBypassed(limiter, true); // keeps its latency: fine
    CHECK(session.takeEditProblem().isEmpty());

    session.discardRecording();
}
