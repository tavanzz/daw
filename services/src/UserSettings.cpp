#include "daw/services/UserSettings.h"

#include <algorithm>
#include <iterator>

namespace daw::services
{

namespace
{

// Keys. Those of version 0 (v0.7.0) keep their names and formats.
constexpr const char* versionKey = "settingsVersion";
constexpr const char* audioDeviceKey = "audioDevice";
constexpr const char* midiInputsOffKey = "midiInputsOff";
constexpr const char* appearanceKey = "appearance";
constexpr const char* pluginWindowsKey = "pluginWindows";
constexpr const char* lastProjectKey = "lastProject";
// Version 1.
constexpr const char* latencyOffsetsKey = "latencyOffsets";
constexpr const char* firstLaunchKey = "firstLaunch";
constexpr const char* crashReportsKey = "crashReports";
constexpr const char* updatesKey = "updates";

// Version 0 kept one key per device pair and rate: "latencyOffset <type>|<in>|<out>|<rate>".
constexpr const char* oldLatencyOffsetPrefix = "latencyOffset ";

juce::PropertiesFile::Options fileOptions(const juce::String& applicationName)
{
    // As v0.7.0 opened the file, so that it reads the same file the same way.
    juce::PropertiesFile::Options options;
    options.applicationName = applicationName;
    options.filenameSuffix = ".settings";
    options.folderName = applicationName;
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

juce::DynamicObject::Ptr objectFrom(const juce::String& json)
{
    const auto parsed = juce::JSON::parse(json);
    if (auto* object = parsed.getDynamicObject())
        return object->clone().release();
    return new juce::DynamicObject();
}

juce::String toJson(const juce::DynamicObject::Ptr& object)
{
    return juce::JSON::toString(juce::var(object.get()), true);
}

// Each migration upgrades the settings of version (its index) to the next version.
using Migration = void (*)(juce::PropertiesFile&);

// 0 -> 1: the latency offsets in one object; the first-launch state, the update and
// crash-report preferences. Settings with a device or a theme were chosen by the user.
void migrateFrom0(juce::PropertiesFile& file)
{
    auto offsets = objectFrom(file.getValue(latencyOffsetsKey));
    const auto keys = file.getAllProperties().getAllKeys();
    for (const auto& key : keys)
        if (key.startsWith(oldLatencyOffsetPrefix))
        {
            offsets->setProperty(key.fromFirstOccurrenceOf(oldLatencyOffsetPrefix, false, false),
                                 file.getIntValue(key));
            file.removeValue(key);
        }
    file.setValue(latencyOffsetsKey, toJson(offsets));

    auto firstLaunch = objectFrom(file.getValue(firstLaunchKey));
    firstLaunch->setProperty("audioDeviceChosen", file.containsKey(audioDeviceKey));
    firstLaunch->setProperty("themeChosen", file.containsKey(appearanceKey));
    file.setValue(firstLaunchKey, toJson(firstLaunch));

    if (!file.containsKey(crashReportsKey))
        file.setValue(crashReportsKey, "undecided");
}

constexpr Migration migrations[] = {migrateFrom0};
static_assert(static_cast<int>(std::size(migrations)) == currentSettingsVersion,
              "every settings version needs a migration from the previous one");

} // namespace

juce::File UserSettings::defaultFolder(const juce::String& applicationName)
{
    return fileOptions(applicationName).getDefaultFile().getParentDirectory();
}

UserSettings::UserSettings(const juce::File& settingsFolder, const juce::String& applicationName)
    : folder(settingsFolder)
{
    folder.createDirectory();
    const auto options = fileOptions(applicationName);
    properties = std::make_unique<juce::PropertiesFile>(
        folder.getChildFile(applicationName + options.filenameSuffix), options);
    versionFound = properties->getIntValue(versionKey, 0);
    migrate();
}

UserSettings::~UserSettings()
{
    save();
}

juce::File UserSettings::getFile() const
{
    return properties->getFile();
}

int UserSettings::getVersion() const
{
    return properties->getIntValue(versionKey, 0);
}

void UserSettings::save()
{
    properties->saveIfNeeded();
}

void UserSettings::migrate()
{
    if (versionFound >= currentSettingsVersion)
        return; // current, or newer: left as it is

    for (auto version = std::max(versionFound, 0); version < currentSettingsVersion; ++version)
        migrations[version](*properties);
    properties->setValue(versionKey, currentSettingsVersion);
    save();
}

std::unique_ptr<juce::XmlElement> UserSettings::getAudioDeviceState() const
{
    return properties->getXmlValue(audioDeviceKey);
}

void UserSettings::setAudioDeviceState(const juce::XmlElement& state)
{
    properties->setValue(audioDeviceKey, &state);
}

juce::StringArray UserSettings::getMidiInputsSwitchedOff() const
{
    auto names = juce::StringArray::fromLines(properties->getValue(midiInputsOffKey));
    names.removeEmptyStrings();
    return names;
}

void UserSettings::setMidiInputsSwitchedOff(const juce::StringArray& names)
{
    properties->setValue(midiInputsOffKey, names.joinIntoString("\n"));
}

juce::String UserSettings::getAppearance() const
{
    return properties->getValue(appearanceKey);
}

void UserSettings::setAppearance(const juce::String& json)
{
    properties->setValue(appearanceKey, json);
}

juce::var UserSettings::getPluginWindows() const
{
    return juce::JSON::parse(properties->getValue(pluginWindowsKey));
}

void UserSettings::setPluginWindows(const juce::var& json)
{
    properties->setValue(pluginWindowsKey, juce::JSON::toString(json, true));
}

juce::File UserSettings::getLastProject() const
{
    const auto path = properties->getValue(lastProjectKey);
    return juce::File::isAbsolutePath(path) ? juce::File(path) : juce::File();
}

void UserSettings::setLastProject(const juce::File& project)
{
    properties->setValue(lastProjectKey, project.getFullPathName());
}

int UserSettings::getLatencyOffset(const juce::String& deviceKey) const
{
    const auto offsets = juce::JSON::parse(properties->getValue(latencyOffsetsKey));
    return static_cast<int>(offsets.getProperty(deviceKey, 0));
}

void UserSettings::setLatencyOffset(const juce::String& deviceKey, int samples)
{
    auto offsets = objectFrom(properties->getValue(latencyOffsetsKey));
    offsets->setProperty(deviceKey, samples);
    properties->setValue(latencyOffsetsKey, toJson(offsets));
}

bool UserSettings::getFirstLaunchFlag(const char* name) const
{
    return static_cast<bool>(juce::JSON::parse(properties->getValue(firstLaunchKey)).getProperty(name, false));
}

void UserSettings::setFirstLaunchFlag(const char* name)
{
    auto firstLaunch = objectFrom(properties->getValue(firstLaunchKey));
    if (static_cast<bool>(firstLaunch->getProperty(name)))
        return;
    firstLaunch->setProperty(name, true);
    properties->setValue(firstLaunchKey, toJson(firstLaunch));
}

bool UserSettings::isAudioDeviceChosen() const
{
    return getFirstLaunchFlag("audioDeviceChosen");
}

void UserSettings::setAudioDeviceChosen()
{
    setFirstLaunchFlag("audioDeviceChosen");
}

bool UserSettings::isThemeChosen() const
{
    return getFirstLaunchFlag("themeChosen");
}

void UserSettings::setThemeChosen()
{
    setFirstLaunchFlag("themeChosen");
}

CrashReportConsent UserSettings::getCrashReportConsent() const
{
    const auto value = properties->getValue(crashReportsKey);
    if (value == "allowed")
        return CrashReportConsent::allowed;
    if (value == "refused")
        return CrashReportConsent::refused;
    return CrashReportConsent::undecided;
}

void UserSettings::setCrashReportConsent(CrashReportConsent consent)
{
    properties->setValue(crashReportsKey, consent == CrashReportConsent::allowed   ? "allowed"
                                          : consent == CrashReportConsent::refused ? "refused"
                                                                                   : "undecided");
}

FirstLaunchPages UserSettings::getFirstLaunchPages(bool crashReportsPossible) const
{
    FirstLaunchPages pages;
    pages.audioDevice = !isAudioDeviceChosen();
    pages.theme = !isThemeChosen();
    pages.crashReports = crashReportsPossible && getCrashReportConsent() == CrashReportConsent::undecided;
    return pages;
}

UpdatePreferences UserSettings::getUpdatePreferences() const
{
    const auto json = juce::JSON::parse(properties->getValue(updatesKey));
    UpdatePreferences preferences;
    preferences.checkAtLaunch = static_cast<bool>(json.getProperty("checkAtLaunch", true));
    preferences.channel = json.getProperty("channel", "stable").toString() == "preRelease"
                              ? UpdateChannel::preRelease
                              : UpdateChannel::stable;
    preferences.skippedVersion = json.getProperty("skippedVersion", {}).toString();
    return preferences;
}

void UserSettings::setUpdatePreferences(const UpdatePreferences& preferences)
{
    // Keys a newer version added stay.
    auto json = objectFrom(properties->getValue(updatesKey));
    json->setProperty("checkAtLaunch", preferences.checkAtLaunch);
    json->setProperty("channel", preferences.channel == UpdateChannel::preRelease ? "preRelease" : "stable");
    json->setProperty("skippedVersion", preferences.skippedVersion);
    properties->setValue(updatesKey, toJson(json));
}

} // namespace daw::services
