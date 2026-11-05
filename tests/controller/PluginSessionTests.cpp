#include "daw/controller/ExportJob.h"
#include "daw/controller/PluginScanner.h"
#include "daw/controller/Session.h"
#include "daw/engine/plugins/RunningPlugin.h"

#include "support/RealtimeCheck.h"
#include "support/TestAudioFiles.h"
#include "support/TestPlugins.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <catch2/catch_test_macros.hpp>

#include <vector>

using daw::controller::ChainOwner;
using daw::controller::PluginCatalogue;
using daw::controller::PluginLoader;
using daw::controller::PluginStatus;
using daw::controller::Session;
using daw::controller::SessionOptions;
using daw::engine::Engine;
using daw::model::Ticks;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr Ticks sample = 5880; // ticks per sample at 48 kHz
constexpr int blockSize = 256;

// The VST3 test plugins, scanned here (they behave).
PluginCatalogue testCatalogue()
{
    PluginCatalogue catalogue;
    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager(formats);
    for (const auto* kind : {"Gain", "Latency", "Synth", "NaN"})
    {
        const daw::controller::ScanItem item{"VST3", daw::test::testVst3(kind).getFullPathName()};
        auto outcome = daw::controller::scanItem(formats, item, {});
        REQUIRE_FALSE(outcome.failure);
        catalogue.addScanned(item, std::move(outcome.plugins));
    }
    return catalogue;
}

juce::PluginDescription described(const PluginCatalogue& catalogue, const juce::String& kind)
{
    for (const auto& info : catalogue.getPlugins())
        if (info.description.name == "DAW Test " + kind)
            return info.description;
    FAIL("no test plugin " << kind.toStdString());
    return {};
}

struct Fixture
{
    explicit Fixture(PluginCatalogue catalogueToUse = testCatalogue())
        : catalogue(std::move(catalogueToUse))
    {
        engine.prepare(sampleRate, blockSize);
        session.setSampleRate(sampleRate);
    }

    SessionOptions options()
    {
        SessionOptions result;
        result.streaming = daw::engine::io::DiskStreamer::Mode::manual;
        result.plugins = &loader;
        result.catalogue = &catalogue;
        return result;
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

    PluginCatalogue catalogue;
    PluginLoader loader{PluginLoader::formatsWithoutEditors()};
    Engine engine;
    Session session{engine, options()};
    std::size_t heapOperations = 0;
};

// A one-second file of a constant value.
juce::File constantFile(const daw::test::TemporaryDirectory& directory, float value)
{
    const auto file = directory.file("constant.wav");
    juce::AudioBuffer<float> samples(2, 48000);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(samples.getWritePointer(channel), value, 48000);
    REQUIRE(daw::test::writeAudioFile(file, daw::test::AudioFileType::wav, samples, sampleRate, 32, true));
    return file;
}

} // namespace

TEST_CASE("A plugin added to a track plays once loaded; its own and the host's changes are undoable",
          "[controller][plugins]")
{
    Fixture fixture;
    auto& session = fixture.session;
    daw::test::TemporaryDirectory directory;
    const auto track = session.addTrack("Audio");
    session.importAudio({constantFile(directory, 0.5f)}, 0, track);
    REQUIRE(session.waitForLoads(10000));

    const auto gain = session.addPlugin({ChainOwner::Kind::track, track}, described(fixture.catalogue, "Gain"));
    REQUIRE(gain != 0);
    CHECK(session.getUndoDescription() == "Add DAW Test Gain");
    fixture.play(2);
    REQUIRE(session.getPluginStatus(gain) == PluginStatus::ready);
    auto* plugin = session.findPlugin(gain);
    REQUIRE(plugin != nullptr);
    REQUIRE(session.getEffectInstance(gain) != nullptr);

    session.play();
    CHECK(fixture.play(8).back() == 0.5f);

    // The host (generic editor) sets it: the plugin follows, undo too.
    const auto parameterId = plugin->getDescription().parameters[0].id;
    session.setEffectParameter(gain, parameterId, 0.25);
    CHECK(plugin->getParameter(0) == 0.25f);
    CHECK(fixture.play(4).back() == 0.25f);
    session.undo();
    CHECK(plugin->getParameter(0) == 0.5f);
    CHECK(fixture.play(4).back() == 0.5f);

    // The plugin's editor drags it: one undo step for the gesture.
    auto& parameter = *plugin->getPlugin().getParameters()[0];
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(0.4f);
    fixture.play(1);
    parameter.setValueNotifyingHost(0.375f);
    fixture.play(1);
    parameter.endChangeGesture();
    fixture.play(1);
    CHECK(session.getEffect(gain)->getParameter(parameterId) == 0.375);
    CHECK(session.getUndoDescription().startsWith("Change"));
    CHECK(fixture.play(4).back() == 0.375f);
    session.undo();
    CHECK(plugin->getParameter(0) == 0.5f);
    CHECK_FALSE(session.getEffect(gain)->getParameter(parameterId).has_value());
    session.redo();
    CHECK(plugin->getParameter(0) == 0.375f);
    CHECK(fixture.heapOperations == 0);
}

TEST_CASE("A removed plugin comes back by undo as it sounded; a new rate keeps the same plugin",
          "[controller][plugins]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto track = session.addTrack("Audio");
    const auto gain = session.addPlugin({ChainOwner::Kind::track, track}, described(fixture.catalogue, "Gain"));
    fixture.play(2);
    auto* plugin = session.findPlugin(gain);
    REQUIRE(plugin != nullptr);

    // Changed without the model knowing (as some plugins do with their state).
    plugin->setParameter(0, 0.125f);
    CHECK(session.removeEffect(gain));
    fixture.play(2);
    CHECK(session.findPlugin(gain) == nullptr);
    session.undo();
    fixture.play(2);
    REQUIRE(session.findPlugin(gain) != nullptr);
    CHECK(session.findPlugin(gain)->getParameter(0) == 0.125f);

    // Another rate: the same plugin, moved to an instance for it.
    auto* before = session.findPlugin(gain);
    const auto oldInstance = session.getEffectInstance(gain);
    fixture.engine.prepare(96000.0, blockSize);
    session.setSampleRate(96000.0);
    fixture.play(2);
    CHECK(session.findPlugin(gain) == before);
    REQUIRE(session.getEffectInstance(gain) != nullptr);
    CHECK(session.getEffectInstance(gain) != oldInstance);
    CHECK(session.getEffectInstance(gain)->getSetup().sampleRate == 96000.0);
    CHECK(before->getParameter(0) == 0.125f);
}

TEST_CASE("A plugin instrument plays the clip's notes, and its latency is compensated",
          "[controller][plugins]")
{
    Fixture fixture;
    auto& session = fixture.session;
    const auto keys = session.addMidiTrack("Keys", "");
    const auto synth = session.setInstrumentPlugin(keys, described(fixture.catalogue, "Synth"));
    REQUIRE(synth != 0);
    CHECK(session.addPlugin({ChainOwner::Kind::track, keys}, described(fixture.catalogue, "Synth")) == 0);
    CHECK(session.setInstrumentPlugin(keys, described(fixture.catalogue, "Gain")) == 0);
    const auto clip = session.addMidiClip(keys, 0, 4800 * sample);
    session.setNotes(clip, {{0, 1000 * sample, 2000 * sample, 60, 127}}, "Note");
    fixture.play(2);
    REQUIRE(session.getPluginStatus(synth) == PluginStatus::ready);

    session.play();
    const auto played = fixture.play(16);
    CHECK(played[999] == 0.0f);
    CHECK(played[1000] == 1.0f);
    CHECK(played[2999] == 1.0f);
    CHECK(played[3000] == 0.0f);

    // A latency plugin after it: the track is late by 777 samples, and so is all.
    session.stop();
    fixture.play(8);
    const auto latency = session.addPlugin({ChainOwner::Kind::track, keys}, described(fixture.catalogue, "Latency"));
    REQUIRE(latency != 0);
    fixture.play(2);
    REQUIRE(session.getPluginStatus(latency) == PluginStatus::ready);
    CHECK(session.getEffectInstance(latency)->getLatency() == 777);
}

TEST_CASE("Plugin states are saved; a missing plugin keeps everything and saves it unchanged",
          "[controller][plugins]")
{
    daw::test::TemporaryDirectory directory;
    const auto file = directory.file("plugins.dawproject");
    juce::String savedJson;
    {
        Fixture fixture;
        auto& session = fixture.session;
        const auto track = session.addTrack("Audio");
        const auto gain = session.addPlugin({ChainOwner::Kind::track, track}, described(fixture.catalogue, "Gain"));
        fixture.play(2);
        auto* plugin = session.findPlugin(gain);
        REQUIRE(plugin != nullptr);
        plugin->setParameter(0, 0.3f);
        fixture.play(1); // the plugin hears of it
        REQUIRE(session.saveProject(file).wasOk());
        savedJson = file.loadFileAsString();
        CHECK(savedJson.contains("\"type\": \"plugin\""));
        CHECK(savedJson.contains("\"name\": \"DAW Test Gain\""));
    }

    // Read back with the plugin installed: it plays with its state.
    {
        Fixture fixture;
        auto& session = fixture.session;
        REQUIRE(session.openProject(file).wasOk());
        fixture.play(2);
        const auto effect = session.getProject().getTrack(0).getEffects().getEffect(0);
        REQUIRE(session.getPluginStatus(effect.getId()) == PluginStatus::ready);
        CHECK(session.findPlugin(effect.getId())->getParameter(0) == 0.3f);
    }

    // Read back where the plugin is not: a placeholder that keeps it all.
    {
        Fixture fixture{PluginCatalogue{}};
        auto& session = fixture.session;
        REQUIRE(session.openProject(file).wasOk());
        fixture.play(2);
        const auto effect = session.getProject().getTrack(0).getEffects().getEffect(0);
        CHECK(session.getPluginStatus(effect.getId()) == PluginStatus::missing);
        CHECK(session.getPluginProblem(effect.getId()).isNotEmpty());
        CHECK(session.findPlugin(effect.getId()) == nullptr);
        CHECK(session.getEffectInstance(effect.getId()) == nullptr); // left out of the chain
        const auto again = directory.file("again.dawproject");
        REQUIRE(session.saveProject(again).wasOk());
        CHECK(again.loadFileAsString() == savedJson);

        // Installed after all (a scan): it loads.
        fixture.catalogue = testCatalogue();
        session.pluginsChanged();
        fixture.play(2);
        CHECK(session.getPluginStatus(effect.getId()) == PluginStatus::ready);
    }
}

TEST_CASE("An export with deterministic plugins equals real-time playback", "[controller][plugins]")
{
    Fixture fixture;
    auto& session = fixture.session;
    daw::test::TemporaryDirectory directory;
    const auto audio = session.addTrack("Audio");
    session.importAudio({constantFile(directory, 0.5f)}, 0, audio);
    const auto gain = session.addPlugin({ChainOwner::Kind::track, audio}, described(fixture.catalogue, "Gain"));
    const auto keys = session.addMidiTrack("Keys", "");
    const auto synth = session.setInstrumentPlugin(keys, described(fixture.catalogue, "Synth"));
    const auto clip = session.addMidiClip(keys, 0, 24000 * sample);
    session.setNotes(clip, {{0, 3000 * sample, 6000 * sample, 64, 64}, {0, 12000 * sample, 3000 * sample, 67, 100}},
                     "Notes");
    REQUIRE(session.waitForLoads(10000));
    fixture.play(2);
    REQUIRE(session.getPluginStatus(gain) == PluginStatus::ready);
    REQUIRE(session.getPluginStatus(synth) == PluginStatus::ready);

    // Automation: the gain steps down at 10000 and back at 20000.
    const auto parameterId = session.findPlugin(gain)->getDescription().parameters[0].id;
    const daw::model::AutomationTarget target =
        daw::model::AutomationTarget::effectParameter(gain, parameterId);
    session.addAutomationPoint({ChainOwner::Kind::track, audio}, target, {0, 0.5, 0.0});
    session.addAutomationPoint({ChainOwner::Kind::track, audio}, target, {10000 * sample, 0.5, 0.0});
    session.addAutomationPoint({ChainOwner::Kind::track, audio}, target, {10000 * sample, 0.25, 0.0});
    session.addAutomationPoint({ChainOwner::Kind::track, audio}, target, {20000 * sample, 0.25, 0.0});
    session.addAutomationPoint({ChainOwner::Kind::track, audio}, target, {20000 * sample, 0.5, 0.0});
    fixture.play(2);

    session.play();
    const auto played = fixture.play(100);
    CHECK(fixture.heapOperations == 0);

    daw::test::TemporaryDirectory exports;
    daw::controller::ExportSettings settings;
    settings.file = exports.file("mix.wav");
    settings.sampleRate = sampleRate;
    settings.bitsPerSample = 32;
    settings.includeTail = false;
    daw::controller::ExportJob job(session, settings);
    CHECK(job.getMissingPlugins().isEmpty());
    const auto result = job.run();
    REQUIRE(result.result.wasOk());

    juce::AudioFormatManager formats;
    formats.registerBasicFormats();
    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(settings.file));
    REQUIRE(reader != nullptr);
    juce::AudioBuffer<float> exported(2, static_cast<int>(reader->lengthInSamples));
    reader->read(&exported, 0, exported.getNumSamples(), 0, true, true);
    REQUIRE(exported.getNumSamples() >= 24000);

    // After the transport's fade-in (240 samples), the same to the sample.
    bool identical = true;
    for (int i = 240; i < 24000; ++i)
        identical = identical && exported.getSample(0, i) == played[static_cast<std::size_t>(i)];
    CHECK(identical);
    CHECK(exported.getSample(0, 5000) == 0.5f + 64.0f / 127.0f);
    CHECK(exported.getSample(0, 14000) == 0.25f + 100.0f / 127.0f);
}

TEST_CASE("A project opens without its plugins, or without the one that crashed; each loads when allowed",
          "[controller][plugins]")
{
    daw::test::TemporaryDirectory directory;
    const auto file = directory.file("plugins.dawproject");
    {
        Fixture fixture;
        auto& session = fixture.session;
        const auto track = session.addTrack("Audio");
        session.addPlugin({ChainOwner::Kind::track, track}, described(fixture.catalogue, "Gain"));
        session.addPlugin({ChainOwner::Kind::track, track}, described(fixture.catalogue, "Latency"));
        REQUIRE(session.saveProject(file).wasOk());
    }

    Fixture fixture;
    auto& session = fixture.session;
    const auto effectIds = [&] {
        const auto effects = session.getProject().getTrack(0).getEffects();
        return std::pair{effects.getEffect(0).getId(), effects.getEffect(1).getId()};
    };

    REQUIRE(session.openProject(file, false).wasOk());
    fixture.play(2);
    auto [gain, latency] = effectIds();
    CHECK(session.getPluginStatus(gain) == PluginStatus::off);
    CHECK(session.getPluginStatus(latency) == PluginStatus::off);
    CHECK(session.getPluginProblem(gain).contains("without plugins"));
    session.allowPlugin(latency);
    fixture.play(2);
    CHECK(session.getPluginStatus(gain) == PluginStatus::off);
    CHECK(session.getPluginStatus(latency) == PluginStatus::ready);
    session.allowAllPlugins();
    fixture.play(2);
    CHECK(session.getPluginStatus(gain) == PluginStatus::ready);

    // The one that was running when the app crashed is kept back on its own.
    session.holdBackPlugins({"DAW Test Gain (VST3)"});
    REQUIRE(session.openProject(file).wasOk());
    fixture.play(2);
    std::tie(gain, latency) = effectIds();
    CHECK(session.getPluginStatus(gain) == PluginStatus::off);
    CHECK(session.getPluginProblem(gain).contains("quit unexpectedly"));
    CHECK(session.getPluginStatus(latency) == PluginStatus::ready);
    session.allowPlugin(gain);
    fixture.play(2);
    CHECK(session.getPluginStatus(gain) == PluginStatus::ready);
}

TEST_CASE("The plugin running now is known, for a crash report", "[engine][plugins]")
{
    using daw::engine::plugins::RunningPlugin;
    CHECK(RunningPlugin::onAudioThread() == nullptr);
    {
        const RunningPlugin outer("Outer (VST3)", true);
        {
            const RunningPlugin inner("Inner (AudioUnit)", true);
            CHECK(juce::String(RunningPlugin::onAudioThread()) == "Inner (AudioUnit)");
            CHECK(RunningPlugin::elsewhere() == nullptr);
        }
        CHECK(juce::String(RunningPlugin::onAudioThread()) == "Outer (VST3)");
    }
    CHECK(RunningPlugin::onAudioThread() == nullptr);
}
