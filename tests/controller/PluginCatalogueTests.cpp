#include "daw/controller/PluginCatalogue.h"

#include "support/TestAudioFiles.h"
#include "support/TestPlugins.h"

#include <catch2/catch_test_macros.hpp>

#include <algorithm>

using daw::controller::PluginCatalogue;
using daw::controller::PluginInfo;
using daw::controller::PluginKey;
using daw::controller::PluginQuery;
using daw::controller::ScanFailure;
using daw::controller::ScanItem;

namespace
{

// The VST3 format without its default folders, so that tests see only their own.
class Vst3InFoldersOnly final : public juce::VST3PluginFormatHeadless
{
public:
    juce::FileSearchPath getDefaultLocationsToSearch() override { return {}; }
};

juce::PluginDescription makeDescription(const juce::String& name, const juce::String& maker, bool instrument,
                                        const juce::String& format, int id, const juce::String& category = {})
{
    juce::PluginDescription description;
    description.name = name;
    description.descriptiveName = name;
    description.manufacturerName = maker;
    description.isInstrument = instrument;
    description.pluginFormatName = format;
    description.uniqueId = id;
    description.category = category;
    description.fileOrIdentifier = "/plugins/" + name + "." + format;
    description.version = "1.0";
    return description;
}

std::vector<juce::String> namesOf(const std::vector<PluginInfo>& plugins)
{
    std::vector<juce::String> names;
    for (const auto& plugin : plugins)
        names.push_back(plugin.description.name + " (" + plugin.description.pluginFormatName + ")");
    return names;
}

// What the scanner would report for a VST3 bundle (read here: test plugins behave).
std::vector<PluginInfo> scanHere(const ScanItem& item)
{
    juce::VST3PluginFormatHeadless format;
    juce::OwnedArray<juce::PluginDescription> found;
    format.findAllTypesForFile(found, item.item);
    std::vector<PluginInfo> infos;
    for (auto* description : found)
        infos.push_back({*description, {}});
    return infos;
}

} // namespace

TEST_CASE("Plugin keys read back what they wrote", "[controller][plugins]")
{
    const PluginKey key{"VST3", -123456};
    CHECK(PluginKey::fromString(key.toString()) == key);
    CHECK(PluginKey::fromString("AudioUnit:42") == PluginKey{"AudioUnit", 42});
    CHECK_FALSE(PluginKey::fromString("VST3"));
    CHECK_FALSE(PluginKey::fromString(":12"));
    CHECK_FALSE(PluginKey::fromString("VST3:twelve"));
}

TEST_CASE("Scanning is incremental: new, changed and gone items; the blacklist stays",
          "[controller][plugins]")
{
    daw::test::TemporaryDirectory directory;
    const auto folder = directory.file("Plugins");
    REQUIRE(folder.createDirectory());
    for (const auto* kind : {"Gain", "Synth"})
        REQUIRE(daw::test::testVst3(kind).copyDirectoryTo(
            folder.getChildFile(daw::test::testVst3(kind).getFileName())));
    const auto gain = folder.getChildFile("DAW Test Gain.vst3").getFullPathName();
    const auto synth = folder.getChildFile("DAW Test Synth.vst3").getFullPathName();
    // A plugin file that is not one (as a broken install leaves): the scanner would
    // fail on it. A plain file, so that its date can be changed on every system.
    const auto broken = folder.getChildFile("Broken.vst3");
    REQUIRE(broken.replaceWithText("not a plugin"));

    juce::AudioPluginFormatManager formats;
    formats.addFormat(std::make_unique<Vst3InFoldersOnly>());
    PluginCatalogue catalogue;
    catalogue.setCustomFolders("VST3", {folder.getFullPathName(), "  ", folder.getFullPathName()});
    CHECK(catalogue.getCustomFolders("VST3") == juce::StringArray{folder.getFullPathName()});

    // At first, everything is new.
    auto plan = catalogue.planScan(formats);
    std::sort(plan.begin(), plan.end());
    REQUIRE(plan == std::vector<ScanItem>{{"VST3", broken.getFullPathName()}, {"VST3", gain}, {"VST3", synth}});
    catalogue.addScanned({"VST3", gain}, scanHere({"VST3", gain}));
    catalogue.addScanned({"VST3", synth}, scanHere({"VST3", synth}));
    catalogue.block({"VST3", broken.getFullPathName()}, ScanFailure::crashed, "signal 6");
    CHECK(catalogue.getPlugins().size() == 2);
    CHECK(catalogue.takeChanged());

    // Nothing changed: nothing to scan; the blacklisted file is left alone.
    CHECK(catalogue.planScan(formats).empty());
    CHECK_FALSE(catalogue.takeChanged());
    CHECK(catalogue.isBlocked({"VST3", broken.getFullPathName()}));

    // A bundle whose date is not the one scanned is scanned again; everything means
    // every item but the blacklisted.
    auto stale = scanHere({"VST3", gain});
    for (auto& info : stale)
        info.description.lastFileModTime = juce::Time(1000);
    catalogue.addScanned({"VST3", gain}, stale);
    CHECK(catalogue.planScan(formats) == std::vector<ScanItem>{{"VST3", gain}});
    plan = catalogue.planScan(formats, true);
    std::sort(plan.begin(), plan.end());
    CHECK(plan == std::vector<ScanItem>{{"VST3", gain}, {"VST3", synth}});
    catalogue.addScanned({"VST3", gain}, scanHere({"VST3", gain}));

    // A blacklisted file that changed gets another chance; so does one retried.
    REQUIRE(broken.setLastModificationTime(juce::Time::getCurrentTime() + juce::RelativeTime::hours(2)));
    CHECK(catalogue.planScan(formats) == std::vector<ScanItem>{{"VST3", broken.getFullPathName()}});
    CHECK_FALSE(catalogue.isBlocked({"VST3", broken.getFullPathName()}));
    catalogue.block({"VST3", synth}, ScanFailure::timedOut);
    CHECK(catalogue.getPlugins().size() == 1);
    catalogue.unblock({"VST3", synth});
    plan = catalogue.planScan(formats);
    std::sort(plan.begin(), plan.end());
    CHECK(plan == std::vector<ScanItem>{{"VST3", broken.getFullPathName()}, {"VST3", synth}});

    // What is removed is forgotten.
    REQUIRE(juce::File(gain).deleteRecursively());
    REQUIRE(broken.deleteFile());
    CHECK(catalogue.planScan(formats) == std::vector<ScanItem>{{"VST3", synth}});
    CHECK(catalogue.getPlugins().empty());
    CHECK(catalogue.getNumScannedItems() == 0);
    CHECK(catalogue.getBlocked().empty());
}

TEST_CASE("The browser's search: words, formats, kinds, manufacturers, favourites",
          "[controller][plugins]")
{
    PluginCatalogue catalogue;
    catalogue.addScanned({"VST3", "/a"}, {{makeDescription("Pro-Q 3", "FabFilter", false, "VST3", 1, "Fx|EQ"), {}},
                                          {makeDescription("Pro-C 2", "FabFilter", false, "VST3", 2, "Fx|Dynamics"), {}}});
    catalogue.addScanned({"AudioUnit", "AudioUnit:b"},
                         {{makeDescription("Pro-Q 3", "FabFilter", false, "AudioUnit", 3, "EQ"), {}}});
    catalogue.addScanned({"VST3", "/c"}, {{makeDescription("Serum 2", "Xfer Records", true, "VST3", 4, "Instrument|Synth"), {}},
                                          {makeDescription("Arp", "Somebody", false, "VST3", 5), "a MIDI effect"}});

    CHECK(namesOf(catalogue.getPlugins()) ==
          std::vector<juce::String>{"Arp (VST3)", "Pro-C 2 (VST3)", "Pro-Q 3 (AudioUnit)", "Pro-Q 3 (VST3)",
                                    "Serum 2 (VST3)"});

    PluginQuery query;
    CHECK(catalogue.search(query).size() == 4); // the unsupported MIDI effect is left out
    query.text = "fab q";
    CHECK(namesOf(catalogue.search(query)) == std::vector<juce::String>{"Pro-Q 3 (AudioUnit)", "Pro-Q 3 (VST3)"});
    query.text = "dynamics";
    CHECK(namesOf(catalogue.search(query)) == std::vector<juce::String>{"Pro-C 2 (VST3)"});
    query.text = "";
    query.formats = {"AudioUnit"};
    CHECK(namesOf(catalogue.search(query)) == std::vector<juce::String>{"Pro-Q 3 (AudioUnit)"});
    query.formats = {};
    query.kind = PluginQuery::Kind::instruments;
    CHECK(namesOf(catalogue.search(query)) == std::vector<juce::String>{"Serum 2 (VST3)"});
    query.kind = PluginQuery::Kind::effects;
    query.manufacturer = "fabfilter";
    CHECK(catalogue.search(query).size() == 3);
    CHECK(catalogue.getManufacturers() == juce::StringArray{"FabFilter", "Xfer Records"});

    query = {};
    query.favouritesOnly = true;
    CHECK(catalogue.search(query).empty());
    catalogue.setFavourite({"VST3", 4}, true);
    catalogue.setFavourite({"VST3", 2}, true);
    catalogue.setFavourite({"VST3", 2}, false);
    CHECK(namesOf(catalogue.search(query)) == std::vector<juce::String>{"Serum 2 (VST3)"});

    REQUIRE(catalogue.find({"AudioUnit", 3}));
    CHECK(catalogue.find({"AudioUnit", 3})->description.name == "Pro-Q 3");
    CHECK_FALSE(catalogue.find({"LV2", 3}));
}

TEST_CASE("The catalogue is saved and read back whole; bad files give an empty one",
          "[controller][plugins]")
{
    PluginCatalogue catalogue;
    catalogue.addScanned({"VST3", "/x.vst3"}, {{makeDescription("A", "M", true, "VST3", 7), {}},
                                               {makeDescription("B", "M", false, "VST3", 8), "more than two channels"}});
    catalogue.block({"AudioUnit", "AudioUnit:Effects/aufx,Bad ,Make"}, ScanFailure::timedOut, "30 s");
    catalogue.setCustomFolders("VST3", {"/my/plugins", "/more"});
    catalogue.setFavourite({"VST3", 7}, true);

    daw::test::TemporaryDirectory directory;
    const auto file = directory.file("Plugins.json");
    REQUIRE(catalogue.saveTo(file));
    const auto read = PluginCatalogue::loadFrom(file);
    CHECK(juce::JSON::toString(read.toJson()) == juce::JSON::toString(catalogue.toJson()));
    CHECK(read.getPlugins().size() == 2);
    CHECK_FALSE(read.getPlugins()[1].isSupported());
    REQUIRE(read.getBlocked().size() == 1);
    CHECK(read.getBlocked()[0].failure == ScanFailure::timedOut);
    CHECK(read.isFavourite({"VST3", 7}));
    CHECK(read.getCustomFolders("VST3") == juce::StringArray{"/my/plugins", "/more"});

    REQUIRE(file.replaceWithText("{ not json"));
    CHECK(PluginCatalogue::loadFrom(file).getPlugins().empty());
    CHECK(PluginCatalogue::loadFrom(directory.file("missing.json")).getPlugins().empty());
}
