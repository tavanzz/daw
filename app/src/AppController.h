#pragma once

#include "CrashMarker.h"
#include "SmokeTest.h"
#include "daw/controller/CoopSession.h"
#include "daw/controller/PluginInstances.h"
#include "daw/controller/PluginLibrary.h"
#include "daw/controller/Session.h"
#include "daw/engine/Engine.h"
#include "daw/engine/io/AudioDeviceHost.h"
#include "daw/services/AppLog.h"
#include "daw/services/CrashReporting.h"
#include "daw/services/NameMigration.h"
#include "daw/services/UpdateChecker.h"
#include "daw/services/UpdateInstaller.h"
#include "daw/services/UserSettings.h"
#include "daw/ui/desktop/MainWindow.h"
#include "daw/ui/desktop/ThemeManager.h"

#include <juce_events/juce_events.h>

#include <memory>

namespace daw::app
{

/**
    Owns the engine, the audio device, the session and the window, and keeps them in
    step: tells the session when the device's sample rate and latencies change,
    drives the session's update() from a timer, keeps the window title current, and
    remembers the audio device and its latency offset (per device pair and rate) and
    the appearance (theme and animations).

    Plugins: the catalogue lives next to the user settings (Plugins.json) and is
    scanned for new or changed plugins at launch, in the background, by the scanner
    program next to the executable; the plugin windows' positions are kept in the user
    settings. If the app crashed inside a plugin, the next start keeps that plugin from
    loading and offers to open the last project with it switched off.

    Inputs: the device opens its inputs only with the microphone permission. The
    permission is asked for the first time a track needs an input (armed, or
    monitoring always), never at launch. MIDI inputs need none: every one is listened
    to (also those plugged in later) unless switched off in the device settings,
    which the user settings remember.

    The user settings (services::UserSettings) are in the user's settings folder, or in
    a folder of its own for diagnostic runs (--settings-dir=), with everything else the
    app keeps there (plugin catalogue, crash marker).

    Message thread only.
*/
class AppController final : private juce::ChangeListener, private juce::Timer
{
public:
    /** How this run differs from a normal launch (the release checks' runs). */
    struct Options
    {
        juce::File settingsFolder;   ///< empty: the user's own
        std::optional<SmokeTest::Options> smokeTest;
        juce::File updateTestResult; ///< --update-test: check, download, install, report here
        juce::StringArray relaunchArguments; ///< what the new version starts with after an update
    };

    AppController(const juce::String& applicationName, Options options);
    ~AppController() override;

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    /** Opens a project, or imports an audio file (e.g. one given on the command line). */
    void openFile(const juce::File& file);

    /** Opens windows for reviewing the interface (see MainComponent::showForReview()). */
    void showForReview(const juce::StringArray& what);

    /** A micinstation:// link (an invite or a server link) the system handed the app. */
    void openLink(const juce::String& link);

    /** Back into the co-op session the app was in when it quit, if any (and the copy still
        belongs to it). */
    void rejoinLastSession();

    /** Quits, after offering to save unsaved changes. */
    void requestQuit();

private:
    [[nodiscard]] static controller::PluginLibrary::Options libraryOptions(const juce::File& folder);
    [[nodiscard]] static std::unique_ptr<juce::AudioPluginFormatManager> pluginFormats();
    [[nodiscard]] controller::SessionOptions sessionOptions();
    void offerProjectWithoutCrashedPlugins(const juce::StringArray& names);
    void updateWindowTitle();
    void saveDeviceState();
    void deviceChanged();
    void midiDevicesChanged();
    void openInputsIfNeeded();
    [[nodiscard]] juce::String latencyOffsetKey();
    [[nodiscard]] ui::desktop::MainComponent& view();

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    [[nodiscard]] static services::NameMigration::Folders foldersFor(const juce::String& name,
                                                                     const juce::File& settingsFolder);
    [[nodiscard]] services::AppLog::Options logOptions() const;
    void logStart(const juce::StringArray& crashedPlugins);
    void startCrashReports(const juce::StringArray& crashedPlugins);
    [[nodiscard]] std::unique_ptr<services::UpdateChecker> makeUpdateChecker() const;
    [[nodiscard]] ui::desktop::AppServices appServices();
    void installUpdate(std::function<void(const juce::String&)> onProblem);
    void watchUpdates();
    void runUpdateTest();
    void finishFirstLaunch(const services::FirstLaunchPages& pages, std::optional<bool> sendCrashReports);
    void logPlugins();
    void logProject();

    const juce::String applicationName;
    const Options runOptions;
    // Before the settings and the log open: the user's data under the app's previous name.
    const services::NameMigration::Folders previousFolders;
    const services::NameMigration::Result nameMigration;
    services::UserSettings settings;
    services::AppLog log; // installed as JUCE's logger: everything after it may log
    CrashMarker crashMarker;
    // After the crash marker: its handler runs first, then hands the crash on to the marker.
    std::unique_ptr<services::CrashReporting> crashReports;

    // Destroyed in reverse order: the window refers to the session, the session to
    // the engine and the plugins, and the device must stop before the engine goes;
    // the plugins' formats outlive every plugin.
    controller::PluginLibrary plugins;
    controller::PluginLoader pluginLoader{pluginFormats()};
    engine::Engine engine;
    engine::io::AudioDeviceHost deviceHost{engine};
    controller::Session session{engine, sessionOptions()};
    std::unique_ptr<controller::CoopSession> coop; // the window shows it
    std::unique_ptr<services::UpdateChecker> updates;
    std::unique_ptr<ui::desktop::ThemeManager> themes; // the window draws with its looks
    std::unique_ptr<ui::desktop::MainWindow> window;

    juce::String shownTitle;
    juce::File shownProject; // the last one kept in the settings
    juce::File shownCoopCopy; // the co-op copy kept in the settings
    bool shownUnsaved = false;
    juce::String loggedDevice, loggedMidiInputs;
    bool loggedScanning = false;
    int ticks = 0;               // timer ticks since the start (the check at launch waits)
    int watchedUpdates = -1;     // the update status last looked at
    bool offeredUpdate = false;  // a release found at launch is offered once
    std::unique_ptr<juce::Thread> updatePreparation;
    std::unique_ptr<SmokeTest> smokeTest;
    bool updateTestStarted = false;

    // Inputs: asking the user, or the device has none (until the device changes).
    bool askingForInputs = false;
    bool noInputsOnDevice = false;
    juce::String offsetKey; // the setting the current latency offset belongs to
    int savedOffset = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

} // namespace daw::app
