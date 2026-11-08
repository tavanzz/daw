#include "daw/services/UserSettings.h"

#include "support/TestAudioFiles.h"

#include <catch2/catch_test_macros.hpp>

using daw::services::CrashReportConsent;
using daw::services::UpdateChannel;
using daw::services::UpdatePreferences;
using daw::services::UserSettings;

namespace
{

// DAW.settings as v0.7.0 wrote it (JUCE's PropertiesFile XML; the device names changed).
constexpr const char* settingsOfV070 = R"(<?xml version="1.0" encoding="UTF-8"?>

<PROPERTIES>
  <VALUE name="audioDevice">
    <DEVICESETUP deviceType="CoreAudio" audioOutputDeviceName="Studio Interface"
                 audioInputDeviceName="" audioDeviceRate="44100.0" audioDeviceBufferSize="512">
      <MIDIINPUT name="Studio Interface MIDI" identifier="1234"/>
    </DEVICESETUP>
  </VALUE>
  <VALUE name="midiInputsOff" val="Old Keyboard&#10;Pad Controller"/>
  <VALUE name="appearance" val="{&quot;theme&quot;:&quot;acid-terminal&quot;,&quot;animations&quot;:{&quot;level&quot;:&quot;reduced&quot;}}"/>
  <VALUE name="pluginWindows" val="{&quot;VST3:7&quot;: [120, 80]}"/>
  <VALUE name="lastProject" val="@PROJECT@"/>
  <VALUE name="latencyOffset CoreAudio|In|Out|44100" val="37"/>
  <VALUE name="latencyOffset CoreAudio|In|Out|48000" val="-12"/>
</PROPERTIES>
)";

juce::File writeSettings(const daw::test::TemporaryDirectory& directory, const juce::String& text)
{
    const auto file = directory.file("DAW.settings");
    REQUIRE(file.replaceWithText(text));
    return file;
}

} // namespace

TEST_CASE("Settings of v0.7.0 are migrated to the current version", "[services][settings]")
{
    daw::test::TemporaryDirectory directory;
    const auto project = directory.file("Music").getChildFile("Song.dawproject");
    const auto file = writeSettings(
        directory, juce::String(settingsOfV070).replace("@PROJECT@", project.getFullPathName()));

    {
        UserSettings settings(file.getParentDirectory(), "DAW");
        CHECK(settings.getFile() == file);
        CHECK(settings.getVersionFound() == 0);
        CHECK(settings.getVersion() == daw::services::currentSettingsVersion);

        const auto device = settings.getAudioDeviceState();
        REQUIRE(device != nullptr);
        CHECK(device->getStringAttribute("audioOutputDeviceName") == "Studio Interface");
        CHECK(settings.getMidiInputsSwitchedOff() == juce::StringArray{"Old Keyboard", "Pad Controller"});
        CHECK(settings.getAppearance().contains("acid-terminal"));
        CHECK(static_cast<int>(settings.getPluginWindows()["VST3:7"][0]) == 120);
        CHECK(settings.getLastProject() == project);
        CHECK(settings.getLatencyOffset("CoreAudio|In|Out|44100") == 37);
        CHECK(settings.getLatencyOffset("CoreAudio|In|Out|48000") == -12);
        CHECK(settings.getLatencyOffset("CoreAudio|In|Out|96000") == 0);

        // The user had a device and a theme: only the crash-report question is open.
        CHECK(settings.isAudioDeviceChosen());
        CHECK(settings.isThemeChosen());
        CHECK(settings.getCrashReportConsent() == CrashReportConsent::undecided);
        CHECK(settings.getUpdatePreferences() == UpdatePreferences{});
    }

    // The old one-key-per-device offsets are gone from the file, which is now version 1.
    const auto text = file.loadFileAsString();
    CHECK_FALSE(text.contains("latencyOffset "));
    CHECK(text.contains("settingsVersion"));

    UserSettings again(file.getParentDirectory(), "DAW");
    CHECK(again.getVersionFound() == daw::services::currentSettingsVersion);
    CHECK(again.getLatencyOffset("CoreAudio|In|Out|44100") == 37);
}

TEST_CASE("New settings start at the current version with nothing decided", "[services][settings]")
{
    daw::test::TemporaryDirectory directory;
    const auto folder = directory.file("Settings");

    UserSettings settings(folder, "DAW");
    CHECK(folder.isDirectory());
    CHECK(settings.getVersion() == daw::services::currentSettingsVersion);
    CHECK_FALSE(settings.isAudioDeviceChosen());
    CHECK_FALSE(settings.isThemeChosen());
    CHECK(settings.getCrashReportConsent() == CrashReportConsent::undecided);
    CHECK(settings.getAudioDeviceState() == nullptr);
    CHECK(settings.getLastProject() == juce::File());
    CHECK(settings.getUpdatePreferences().checkAtLaunch);
}

TEST_CASE("Every setting reads back what was written, also after reopening", "[services][settings]")
{
    daw::test::TemporaryDirectory directory;
    const auto folder = directory.file("Settings");

    UpdatePreferences updates;
    updates.checkAtLaunch = false;
    updates.channel = UpdateChannel::preRelease;
    updates.skippedVersion = "0.9.1";

    {
        UserSettings settings(folder, "DAW");
        juce::XmlElement device("DEVICESETUP");
        device.setAttribute("audioOutputDeviceName", "Speakers");
        settings.setAudioDeviceState(device);
        settings.setMidiInputsSwitchedOff({"A", "B"});
        settings.setAppearance(R"({"theme":"synth-chrome"})");
        settings.setPluginWindows(juce::JSON::parse(R"({"AudioUnit:1": [1, 2]})"));
        settings.setLastProject(directory.file("x.dawproject"));
        settings.setLatencyOffset("ASIO|a|b|48000", 5);
        settings.setAudioDeviceChosen();
        settings.setThemeChosen();
        settings.setCrashReportConsent(CrashReportConsent::refused);
        settings.setUpdatePreferences(updates);
    }

    UserSettings settings(folder, "DAW");
    CHECK(settings.getAudioDeviceState()->getStringAttribute("audioOutputDeviceName") == "Speakers");
    CHECK(settings.getMidiInputsSwitchedOff() == juce::StringArray{"A", "B"});
    CHECK(settings.getAppearance() == R"({"theme":"synth-chrome"})");
    CHECK(static_cast<int>(settings.getPluginWindows()["AudioUnit:1"][1]) == 2);
    CHECK(settings.getLastProject() == directory.file("x.dawproject"));
    CHECK(settings.getLatencyOffset("ASIO|a|b|48000") == 5);
    CHECK(settings.isAudioDeviceChosen());
    CHECK(settings.isThemeChosen());
    CHECK(settings.getCrashReportConsent() == CrashReportConsent::refused);
    CHECK(settings.getUpdatePreferences() == updates);

    settings.setCrashReportConsent(CrashReportConsent::allowed);
    CHECK(settings.getCrashReportConsent() == CrashReportConsent::allowed);
}

TEST_CASE("Settings from a newer version are read, never downgraded, unknown keys kept",
          "[services][settings]")
{
    daw::test::TemporaryDirectory directory;
    const auto newerVersion = daw::services::currentSettingsVersion + 1;
    const auto file = writeSettings(directory, juce::String(R"(<?xml version="1.0" encoding="UTF-8"?>
<PROPERTIES>
  <VALUE name="settingsVersion" val=")") + juce::String(newerVersion) + R"("/>
  <VALUE name="futureKey" val="kept"/>
  <VALUE name="appearance" val="{&quot;theme&quot;:&quot;neon-grid&quot;}"/>
  <VALUE name="updates" val="{&quot;checkAtLaunch&quot;: false, &quot;futureOption&quot;: 3}"/>
  <VALUE name="crashReports" val="allowed"/>
</PROPERTIES>
)");

    {
        UserSettings settings(file.getParentDirectory(), "DAW");
        CHECK(settings.getVersionFound() == newerVersion);
        CHECK(settings.getVersion() == newerVersion);
        CHECK(settings.getAppearance().contains("neon-grid"));
        CHECK_FALSE(settings.getUpdatePreferences().checkAtLaunch);
        CHECK(settings.getCrashReportConsent() == CrashReportConsent::allowed);

        auto updates = settings.getUpdatePreferences();
        updates.skippedVersion = "2.0.0";
        settings.setUpdatePreferences(updates);
        settings.setLatencyOffset("x", 1);
    }

    const auto xml = juce::parseXML(file);
    REQUIRE(xml != nullptr);
    juce::PropertySet written;
    for (auto* value : xml->getChildIterator())
        written.setValue(value->getStringAttribute("name"), value->getStringAttribute("val"));

    CHECK(written.getIntValue("settingsVersion") == newerVersion);
    CHECK(written.getValue("futureKey") == "kept");
    const auto updates = juce::JSON::parse(written.getValue("updates"));
    CHECK(static_cast<int>(updates["futureOption"]) == 3);
    CHECK(updates["skippedVersion"].toString() == "2.0.0");
}

TEST_CASE("The first launch asks only what is still undecided", "[services][settings]")
{
    daw::test::TemporaryDirectory directory;
    UserSettings settings(directory.file("Settings"), "DAW");

    auto pages = settings.getFirstLaunchPages(true);
    CHECK(pages.audioDevice);
    CHECK(pages.theme);
    CHECK(pages.crashReports);
    CHECK_FALSE(settings.getFirstLaunchPages(false).crashReports); // a build that sends none

    settings.setAudioDeviceChosen();
    settings.setThemeChosen();
    pages = settings.getFirstLaunchPages(true);
    CHECK_FALSE(pages.audioDevice);
    CHECK_FALSE(pages.theme);
    CHECK(pages.any());

    settings.setCrashReportConsent(CrashReportConsent::refused);
    CHECK_FALSE(settings.getFirstLaunchPages(true).any());
}
