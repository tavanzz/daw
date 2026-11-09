#pragma once

#include <juce_core/juce_core.h>
#include <juce_data_structures/juce_data_structures.h>

#include <memory>

namespace daw::services
{

/** Version of the user settings written by this build. Bump it whenever what a key
    holds changes, and add a migration from the previous version (UserSettings.cpp). */
inline constexpr int currentSettingsVersion = 1;

/** Whether crash reports may be sent. Undecided until the user answers the first-launch
    question; nothing is ever sent unless allowed. */
enum class CrashReportConsent
{
    undecided,
    allowed,
    refused
};

/** Which releases the update check looks at. */
enum class UpdateChannel
{
    stable,     ///< releases only
    preRelease  ///< release candidates too
};

/** The pages the first launch shows: only what the user has not decided yet. */
struct FirstLaunchPages
{
    bool audioDevice = false;
    bool theme = false;
    bool crashReports = false;

    [[nodiscard]] bool any() const noexcept { return audioDevice || theme || crashReports; }
};

struct UpdatePreferences
{
    bool checkAtLaunch = true;
    UpdateChannel channel = UpdateChannel::stable;
    juce::String skippedVersion; ///< "Skip This Version": not offered again

    bool operator==(const UpdatePreferences&) const = default;
};

/**
    The user's settings (not a project's): one file per user, `DAW.settings`, in the
    settings folder (`defaultFolder()`, or a folder of its own for diagnostic runs).
    Other files of the app live next to it (the plugin catalogue, the crash marker).

    Versioned like project files: the file carries "settingsVersion", and opening it runs
    the migrations from its version to the current one. A file from a newer version is
    read as far as this version understands it: its version is never lowered and keys
    this version does not know are kept.

    Message thread only.
*/
class UserSettings
{
public:
    /** The folder the settings live in for this user. */
    [[nodiscard]] static juce::File defaultFolder(const juce::String& applicationName);

    /** Opens the settings in `folder` (created if needed) and migrates them. */
    UserSettings(const juce::File& folder, const juce::String& applicationName);
    ~UserSettings();

    UserSettings(const UserSettings&) = delete;
    UserSettings& operator=(const UserSettings&) = delete;

    [[nodiscard]] juce::File getFolder() const { return folder; }
    [[nodiscard]] juce::File getFile() const;

    /** The version the file had when opened (0: written by v0.7.0 or earlier, or new). */
    [[nodiscard]] int getVersionFound() const noexcept { return versionFound; }

    /** The version the file has now: the current one, or a newer one left as it was. */
    [[nodiscard]] int getVersion() const;

    /** Writes pending changes now (they are also written a moment after each change). */
    void save();

    // The audio device as JUCE's AudioDeviceManager describes it.
    [[nodiscard]] std::unique_ptr<juce::XmlElement> getAudioDeviceState() const;
    void setAudioDeviceState(const juce::XmlElement& state);

    [[nodiscard]] juce::StringArray getMidiInputsSwitchedOff() const;
    void setMidiInputsSwitchedOff(const juce::StringArray& names);

    /** The theme and animations, as the appearance module writes them (JSON). */
    [[nodiscard]] juce::String getAppearance() const;
    void setAppearance(const juce::String& json);

    /** Where plugin windows were, as the desktop UI writes it (JSON). */
    [[nodiscard]] juce::var getPluginWindows() const;
    void setPluginWindows(const juce::var& json);

    [[nodiscard]] juce::File getLastProject() const;
    void setLastProject(const juce::File& project);

    /** The manual latency offset, in samples, for one device pair and rate. */
    [[nodiscard]] int getLatencyOffset(const juce::String& deviceKey) const;
    void setLatencyOffset(const juce::String& deviceKey, int samples);

    // First launch: what the user has decided already.
    [[nodiscard]] bool isAudioDeviceChosen() const;
    void setAudioDeviceChosen();
    [[nodiscard]] bool isThemeChosen() const;
    void setThemeChosen();

    [[nodiscard]] CrashReportConsent getCrashReportConsent() const;
    void setCrashReportConsent(CrashReportConsent consent);

    /** What the first launch still has to ask; the crash-report question only when the
        build can send reports at all. */
    [[nodiscard]] FirstLaunchPages getFirstLaunchPages(bool crashReportsPossible) const;

    [[nodiscard]] UpdatePreferences getUpdatePreferences() const;
    void setUpdatePreferences(const UpdatePreferences& preferences);

private:
    void migrate();
    void setFirstLaunchFlag(const char* name);
    [[nodiscard]] bool getFirstLaunchFlag(const char* name) const;

    juce::File folder;
    std::unique_ptr<juce::PropertiesFile> properties;
    int versionFound = 0;
};

} // namespace daw::services
