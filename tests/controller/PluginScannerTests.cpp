#include "daw/controller/PluginLibrary.h"
#include "daw/controller/PluginScanner.h"

#include "support/TestAudioFiles.h"
#include "support/TestPlugins.h"

#include <catch2/catch_test_macros.hpp>

#include <map>

using daw::controller::PluginCatalogue;
using daw::controller::PluginScanner;
using daw::controller::ScanFailure;
using daw::controller::ScanItem;
using daw::controller::ScanOutcome;

namespace
{

// Long enough for a busy machine to start the scanner three times at once; the plugin
// that hangs costs this much.
constexpr int hangTimeout = 8000;

PluginScanner::Options scannerOptions(int timeoutMilliseconds = 30000)
{
    PluginScanner::Options options;
    options.executable = juce::File(DAW_PLUGIN_SCANNER);
    options.processes = 3;
    options.timeoutMilliseconds = timeoutMilliseconds;
    return options;
}

std::map<ScanItem, ScanOutcome> scanAll(PluginScanner& scanner, const std::vector<ScanItem>& items,
                                        const std::map<juce::String, juce::StringArray>& folders = {})
{
    scanner.scan(items, folders);
    REQUIRE(scanner.waitUntilDone(120000));
    std::map<ScanItem, ScanOutcome> outcomes;
    for (auto& result : scanner.takeResults())
        outcomes[result.item] = std::move(result.outcome);
    return outcomes;
}

ScanItem vst3(const juce::File& bundle)
{
    return {"VST3", bundle.getFullPathName()};
}

// VST3 without its default folders, so that tests see only their own.
class Vst3InFoldersOnly final : public juce::VST3PluginFormatHeadless
{
public:
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
};

} // namespace

TEST_CASE("Scanning an item here lists its plugins and the layout they take", "[controller][plugins]")
{
    juce::AudioPluginFormatManager formats;
    juce::addHeadlessDefaultFormatsToManager(formats);

    const auto gain = daw::controller::scanItem(formats, vst3(daw::test::testVst3("Gain")), {});
    CHECK_FALSE(gain.failure);
    REQUIRE(gain.plugins.size() == 1);
    CHECK(gain.plugins[0].isSupported());
    CHECK(gain.plugins[0].description.name == "DAW Test Gain");
    CHECK(gain.plugins[0].description.numInputChannels == 2);
    CHECK(gain.plugins[0].description.numOutputChannels == 2);

    const auto synth = daw::controller::scanItem(formats, vst3(daw::test::testVst3("Synth")), {});
    REQUIRE(synth.plugins.size() == 1);
    CHECK(synth.plugins[0].description.isInstrument);
    CHECK(synth.plugins[0].description.numInputChannels == 0);
    CHECK(synth.plugins[0].description.numOutputChannels == 2);

    const auto nothing = daw::controller::scanItem(formats, {"VST3", "/no/such/plugin.vst3"}, {});
    CHECK(nothing.failure == ScanFailure::noPlugins);
    const auto unknown = daw::controller::scanItem(formats, {"CLAP", "/x.clap"}, {});
    CHECK(unknown.failure == ScanFailure::failedToLoad);
}

TEST_CASE("The scanner's outcome reads back what it wrote, and only when complete",
          "[controller][plugins]")
{
    ScanOutcome outcome;
    juce::PluginDescription description;
    description.name = "X";
    description.pluginFormatName = "VST3";
    description.fileOrIdentifier = "/x.vst3";
    outcome.plugins.push_back({description, "a MIDI effect"});
    outcome.detail = "something";
    const auto read = daw::controller::outcomeFromJson(daw::controller::outcomeToJson(outcome));
    REQUIRE(read);
    REQUIRE(read->plugins.size() == 1);
    CHECK(read->plugins[0].unsupported == "a MIDI effect");
    CHECK_FALSE(read->failure);
    CHECK(read->detail == "something");

    outcome.failure = ScanFailure::noPlugins;
    CHECK(daw::controller::outcomeFromJson(daw::controller::outcomeToJson(outcome))->failure ==
          ScanFailure::noPlugins);
    CHECK_FALSE(daw::controller::outcomeFromJson(juce::JSON::parse("{\"plugins\": []}")));
    CHECK_FALSE(daw::controller::outcomeFromJson({}));
}

TEST_CASE("Plugins that crash or hang when scanned are blacklisted and the app carries on",
          "[controller][plugins][scanner]")
{
    PluginScanner scanner(scannerOptions(hangTimeout));
    const auto gain = vst3(daw::test::testVst3("Gain"));
    const auto synth = vst3(daw::test::testVst3("Synth"));
    const auto crash = vst3(daw::test::faultyTestVst3("Crash"));
    const auto hang = vst3(daw::test::faultyTestVst3("Hang"));

    const auto started = juce::Time::getMillisecondCounter();
    auto outcomes = scanAll(scanner, {crash, gain, hang, synth});
    // Three at once: the hang costs its time once, not the others' time.
    CHECK(juce::Time::getMillisecondCounter() - started < 2 * hangTimeout);

    REQUIRE(outcomes.size() == 4);
    CHECK(outcomes[crash].failure == ScanFailure::crashed);
    CHECK(outcomes[hang].failure == ScanFailure::timedOut);
    CHECK_FALSE(outcomes[gain].failure);
    REQUIRE(outcomes[gain].plugins.size() == 1);
    CHECK(outcomes[gain].plugins[0].description.name == "DAW Test Gain");
    REQUIRE(outcomes[synth].plugins.size() == 1);
    CHECK(outcomes[synth].plugins[0].description.isInstrument);

    const auto progress = scanner.getProgress();
    CHECK(progress.done == 4);
    CHECK(progress.total == 4);
    CHECK(progress.current.isEmpty());

    // Into the catalogue as the app does it.
    PluginCatalogue catalogue;
    for (auto& [item, outcome] : outcomes)
    {
        if (outcome.failure)
            catalogue.block(item, *outcome.failure, outcome.detail);
        else
            catalogue.addScanned(item, outcome.plugins);
    }
    CHECK(catalogue.getPlugins().size() == 2);
    CHECK(catalogue.getBlocked().size() == 2);
    CHECK(catalogue.isBlocked(crash));
    CHECK(catalogue.isBlocked(hang));
}

TEST_CASE("Cancelling stops the scan at once", "[controller][plugins][scanner]")
{
    PluginScanner scanner(scannerOptions(60000));
    scanner.scan({vst3(daw::test::faultyTestVst3("Hang")), vst3(daw::test::testVst3("Gain"))});
    juce::Thread::sleep(200);
    CHECK(scanner.isScanning());
    scanner.cancel();
    CHECK(scanner.waitUntilDone(5000));
    CHECK_FALSE(scanner.isScanning());
}

#if JUCE_MAC
TEST_CASE("An Audio Unit of macOS is scanned in the scanner process", "[controller][plugins][scanner]")
{
    PluginScanner scanner(scannerOptions());
    const ScanItem delay{"AudioUnit", "AudioUnit:Effects/aufx,dely,appl"};
    auto outcomes = scanAll(scanner, {delay});
    REQUIRE(outcomes.size() == 1);
    CHECK_FALSE(outcomes[delay].failure);
    REQUIRE(outcomes[delay].plugins.size() == 1);
    CHECK(outcomes[delay].plugins[0].description.manufacturerName == "Apple");
    CHECK(outcomes[delay].plugins[0].isSupported());
}
#endif

#if JUCE_PLUGINHOST_LV2
TEST_CASE("LV2 plugins are scanned with their folders; faulty ones are blacklisted",
          "[controller][plugins][scanner]")
{
    PluginScanner scanner(scannerOptions(hangTimeout));
    const auto good = daw::test::testPluginsFolder().getChildFile("LV2").getFullPathName();
    const auto faulty = daw::test::testPluginsFolder().getChildFile("Faulty/LV2").getFullPathName();
    const std::map<juce::String, juce::StringArray> folders{{"LV2", {good, faulty}}};
    const ScanItem gain{"LV2", "urn:daw:test:gain"};
    const ScanItem crash{"LV2", "urn:daw:test:crash"};
    const ScanItem hang{"LV2", "urn:daw:test:hang"};
    auto outcomes = scanAll(scanner, {gain, crash, hang}, folders);
    REQUIRE(outcomes.size() == 3);
    CHECK_FALSE(outcomes[gain].failure);
    CHECK(outcomes[crash].failure == ScanFailure::crashed);
    CHECK(outcomes[hang].failure == ScanFailure::timedOut);
}
#endif

TEST_CASE("The library scans its folders once, keeps what it found and scans what is new",
          "[controller][plugins][scanner]")
{
    daw::test::TemporaryDirectory directory;
    daw::controller::PluginLibrary::Options options;
    options.catalogueFile = directory.file("Plugins.json");
    options.scanner = scannerOptions(hangTimeout);
    options.addFormats = [](juce::AudioPluginFormatManager& formats) {
        formats.addFormat(std::make_unique<Vst3InFoldersOnly>());
    };
    const auto folders = juce::StringArray{daw::test::testPluginsFolder().getChildFile("VST3").getFullPathName(),
                                           daw::test::testPluginsFolder().getChildFile("Faulty/VST3").getFullPathName()};
    {
        daw::controller::PluginLibrary library(options);
        library.getCatalogue().setCustomFolders("VST3", folders);
        REQUIRE(library.scanAndWait(false, 60000));
        CHECK(library.getCatalogue().getPlugins().size() == 4);
        CHECK(library.getCatalogue().getBlocked().size() == 2);
        CHECK(options.catalogueFile.existsAsFile());
    }

    // Read back from its file: nothing new, nothing to scan.
    daw::controller::PluginLibrary library(options);
    CHECK(library.getCatalogue().getPlugins().size() == 4);
    CHECK(library.getCatalogue().getCustomFolders("VST3") == folders);
    library.scan();
    CHECK_FALSE(library.isScanning());
    CHECK_FALSE(library.update());

    // A retry scans the item again (and it crashes again).
    const auto crash = vst3(daw::test::faultyTestVst3("Crash"));
    library.retry(crash);
    CHECK_FALSE(library.getCatalogue().isBlocked(crash));
    REQUIRE(library.scanAndWait(false, 60000));
    CHECK(library.getCatalogue().isBlocked(crash));
}
