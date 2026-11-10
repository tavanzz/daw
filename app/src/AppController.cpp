#include "AppController.h"

#include "daw/engine/io/MicrophoneAccess.h"
#include "daw/services/BuildInfo.h"
#include "daw/services/Ed25519.h"
#include "daw/ui/desktop/Alerts.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace daw::app
{

namespace
{
constexpr int updateRateHz = 60;
} // namespace

controller::PluginLibrary::Options AppController::libraryOptions(const juce::File& folder)
{
    controller::PluginLibrary::Options options;
    options.catalogueFile = folder.getChildFile("Plugins.json");
    options.scanner.executable =
        juce::File::getSpecialLocation(juce::File::currentExecutableFile).getSiblingFile(DAW_PLUGIN_SCANNER_NAME);
    return options;
}

std::unique_ptr<juce::AudioPluginFormatManager> AppController::pluginFormats()
{
    // The formats with editors: plugins open their own windows.
    auto formats = std::make_unique<juce::AudioPluginFormatManager>();
    juce::addDefaultFormatsToManager(*formats);
    return formats;
}

controller::SessionOptions AppController::sessionOptions()
{
    controller::SessionOptions options;
    options.plugins = &pluginLoader;
    options.catalogue = &plugins.getCatalogue();
    return options;
}

services::NameMigration::Folders AppController::foldersFor(const juce::String& name, const juce::File& settingsFolder)
{
    // A run with settings of its own keeps everything there, logs included, whatever the
    // app is called.
    if (settingsFolder != juce::File())
        return {name, settingsFolder, settingsFolder.getChildFile("Logs")};
    return {name, services::UserSettings::defaultFolder(name), services::AppLog::defaultFolder(name)};
}

services::AppLog::Options AppController::logOptions() const
{
    services::AppLog::Options options;
    options.applicationName = applicationName;
    options.folder = foldersFor(applicationName, runOptions.settingsFolder).logs;
    return options;
}

AppController::AppController(const juce::String& name, Options options)
    : applicationName(name)
    , runOptions(std::move(options))
    , previousFolders(foldersFor(services::buildInfo().previousProductName, runOptions.settingsFolder))
    , nameMigration(services::NameMigration::run(previousFolders, foldersFor(name, runOptions.settingsFolder)))
    , settings(foldersFor(name, runOptions.settingsFolder).settings, name)
    , log(logOptions())
    , crashMarker(settings.getFolder().getChildFile("CrashedPlugins.txt"))
    , plugins(libraryOptions(settings.getFolder()))
{
    // A plugin that crashed the app last time stays off until the user loads it.
    const auto crashed = crashMarker.takeCrashedPlugins();
    session.holdBackPlugins(crashed);
    logStart(crashed);
    startCrashReports(crashed);

    // The theme applies before the first window opens, so nothing flashes in another.
    themes = std::make_unique<ui::desktop::ThemeManager>(
        appearance::AppearancePreferences::fromJson(settings.getAppearance()),
        [this](const appearance::AppearancePreferences& preferences)
        {
            settings.setAppearance(preferences.toJson());
            settings.setThemeChosen();
            settings.save();
        });

    // Inputs open at launch only if the user already allowed them: never a prompt.
    using engine::io::MicrophoneAccess;
    const auto access = MicrophoneAccess::getStatus();
    session.setInputAccess(access == MicrophoneAccess::Status::granted
                               ? controller::Session::InputAccess::granted
                           : access == MicrophoneAccess::Status::denied
                               ? controller::Session::InputAccess::denied
                               : controller::Session::InputAccess::unknown);

    // The release checks' runs open no audio device: build machines have none, and on a
    // computer whose interface also has inputs macOS may first ask for the microphone,
    // which nobody answers there. They load audio at 48 kHz instead.
    const bool releaseCheck = runOptions.smokeTest.has_value() || runOptions.updateTestResult != juce::File();
    juce::String deviceError;
    if (releaseCheck)
    {
        juce::Logger::writeToLog("Audio device: none (release check), audio loaded at 48 kHz");
        session.setSampleRate(48000.0);
    }
    else
    {
        const auto savedDevice = settings.getAudioDeviceState();
        deviceError = deviceHost.initialise(savedDevice.get(), access == MicrophoneAccess::Status::granted);
        deviceHost.getDeviceManager().addChangeListener(this);
        deviceChanged();
    }

    // Every MIDI input is listened to, also those plugged in later, unless the user
    // switched it off in the device settings.
    deviceHost.onMidiDevicesChanged = [this] { midiDevicesChanged(); };
    deviceHost.openMidiInputs(settings.getMidiInputsSwitchedOff());
    midiDevicesChanged();

    controller::CoopSession::Options coopOptions;
    coopOptions.appVersion = services::buildInfo().version;
    coop = std::make_unique<controller::CoopSession>(session, controller::CoopSession::tlsLinks(), coopOptions);

    window = std::make_unique<ui::desktop::MainWindow>(applicationName, session,
                                                       deviceHost.getDeviceManager(), *themes, &plugins);
    updates = makeUpdateChecker();
    view().setAppServices(appServices());
    view().setCoopSession(coop.get());

    auto& positions = view().getPluginWindowPositions();
    positions = ui::desktop::WindowPositions::fromJson(settings.getPluginWindows());
    positions.onChange = [this] {
        settings.setPluginWindows(view().getPluginWindowPositions().toJson());
        settings.save();
    };

    if (runOptions.smokeTest)
        smokeTest = std::make_unique<SmokeTest>(*runOptions.smokeTest, session, plugins,
                                                settings.getFolder().getChildFile("Smoke"));

    // New and changed plugins are found in the background (a release check scans only
    // what it needs, itself).
    if (!releaseCheck)
        plugins.scan();

    if (deviceError.isNotEmpty())
    {
        if (releaseCheck)
            juce::Logger::writeToLog("No audio output: " + deviceError);
        else
            view().showError("No audio output", "The audio output could not be opened: " + deviceError +
                                                    "\n\nChoose another one in File > Audio Settings.");
    }

    // The first launch asks what is still undecided (not in the release checks' runs).
    if (const auto pages = settings.getFirstLaunchPages(crashReports->canSend()); pages.any() && !releaseCheck)
        view().showFirstLaunch(pages, [this, pages](std::optional<bool> sendCrashReports) {
            finishFirstLaunch(pages, sendCrashReports);
        });

    updateWindowTitle();
    startTimerHz(updateRateHz);

    if (!crashed.isEmpty())
        offerProjectWithoutCrashedPlugins(crashed);
}

void AppController::offerProjectWithoutCrashedPlugins(const juce::StringArray& names)
{
    const auto list = names.joinIntoString(", ");
    const auto project = settings.getLastProject();
    const auto message = "The app quit unexpectedly while " + list + (names.size() == 1 ? " was" : " were") +
                         " running. " + (names.size() == 1 ? "It stays" : "They stay") +
                         " switched off in the projects you open now: load " +
                         (names.size() == 1 ? "it" : "them") +
                         " from the mixer slot (right-click, Load Plugin) when you want.";
    if (!project.existsAsFile())
    {
        view().showError("A plugin crashed", message);
        return;
    }

    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::WarningIcon)
                             .withTitle("A plugin crashed")
                             .withMessage(message + "\n\nOpen \"" + project.getFileNameWithoutExtension() +
                                          "\" again?")
                             .withButton("Open")
                             .withButton("Not Now")
                             .withAssociatedComponent(&view());
    ui::desktop::showAlert(options, [this, project, stillHere = alive](int button) {
        if (*stillHere && button == 0)
            view().openFile(project);
    });
}

AppController::~AppController()
{
    *alive = false;
    stopTimer();
    if (!runOptions.smokeTest && runOptions.updateTestResult == juce::File())
        saveDeviceState();
    deviceHost.getDeviceManager().removeChangeListener(this);
    window.reset();
    if (coop != nullptr && coop->isActive())
        if (const auto saved = coop->saveCopy(); saved.failed())
            juce::Logger::writeToLog("Co-op: the copy could not be saved: " + saved.getErrorMessage());
    coop.reset();
    juce::Logger::writeToLog(services::AppLog::endOfSessionLine);
    juce::Logger::setCurrentLogger(nullptr);
}

void AppController::logStart(const juce::StringArray& crashedPlugins)
{
    juce::Logger::setCurrentLogger(&log);
    const auto& build = services::buildInfo();
    juce::Logger::writeToLog(build.describe() + ", commit of " + build.commitDate);
    juce::Logger::writeToLog("System: " + juce::SystemStats::getOperatingSystemName() + ", " +
                             juce::SystemStats::getCpuModel() + ", " +
                             juce::String(juce::SystemStats::getNumCpus()) + " cores, " +
                             juce::String(juce::SystemStats::getMemorySizeInMegabytes()) + " MB");
    if (settings.getVersionFound() != settings.getVersion())
        juce::Logger::writeToLog("Settings migrated from version " + juce::String(settings.getVersionFound()) +
                                 " to " + juce::String(settings.getVersion()));
    else
        juce::Logger::writeToLog("Settings version " + juce::String(settings.getVersion()));
    if (const auto migration = nameMigration.describe(previousFolders); migration.isNotEmpty())
        juce::Logger::writeToLog(migration);
    if (!log.previousSessionEndedNormally())
        juce::Logger::writeToLog("The previous session did not end normally");
    if (!crashedPlugins.isEmpty())
        juce::Logger::writeToLog("Kept off after a crash: " + crashedPlugins.joinIntoString(", "));
}

void AppController::logPlugins()
{
    // A scan is logged once it is over, with the plugins that could not be loaded.
    const auto scanning = plugins.isScanning();
    if (scanning || !loggedScanning)
    {
        loggedScanning = scanning;
        return;
    }
    loggedScanning = false;

    const auto& catalogue = plugins.getCatalogue();
    juce::Logger::writeToLog("Plugin scan finished: " + juce::String(catalogue.getPlugins().size()) +
                             " plugins, " + juce::String(catalogue.getBlocked().size()) + " not loaded");
    for (const auto& blocked : catalogue.getBlocked())
        juce::Logger::writeToLog("  not loaded: " + blocked.item.item + " (" + blocked.detail + ")");
}

void AppController::startCrashReports(const juce::StringArray& crashedPlugins)
{
    const auto& build = services::buildInfo();
    services::CrashReporting::Options options;
    options.databaseFolder = settings.getFolder().getChildFile("Crash Reporting");
    options.reportsFolder = log.getFolder().getChildFile("Crashes");
    options.dsn = build.crashReportsDsn;
    options.release = build.bundleId + "@" + build.version;
    options.environment = build.buildType == "Debug"   ? "development"
                          : build.version.contains("-") ? "pre-release"
                                                        : "release";
    options.crashedPlugins = crashedPlugins;
    options.sendAllowed = settings.getCrashReportConsent() == services::CrashReportConsent::allowed;
    crashReports = std::make_unique<services::CrashReporting>(options);
    juce::Logger::writeToLog(juce::String("Crash reports: kept locally") +
                             (!crashReports->canSend()     ? ", this build sends none"
                              : options.sendAllowed        ? ", sent (the user allowed it)"
                                                           : ", not sent (the user has not allowed it)"));
}

std::unique_ptr<services::UpdateChecker> AppController::makeUpdateChecker() const
{
    const auto& build = services::buildInfo();
    services::UpdateChecker::Options options;
    // For checking the updater by hand or in the release checks: another version to start
    // from, another manifest to read. Harmless: what gets installed still needs a valid
    // signature by the key built into the app.
    options.currentVersion = juce::SystemStats::getEnvironmentVariable("DAW_PRETEND_VERSION", build.version);
    options.feedOverride = juce::SystemStats::getEnvironmentVariable("DAW_UPDATE_FEED", {});
    options.updateId = build.updateId;
    options.publicKey = services::updatePublicKey();
    options.repositoryUrl = build.repositoryUrl;
    options.downloadFolder = juce::File::getSpecialLocation(juce::File::tempDirectory)
                                 .getChildFile(applicationName + " Updates");
    if (options.currentVersion != build.version || options.feedOverride.isNotEmpty())
        juce::Logger::writeToLog("Updates: pretending to be " + options.currentVersion + ", reading " +
                                 (options.feedOverride.isNotEmpty() ? options.feedOverride : juce::String("GitHub")));
    return std::make_unique<services::UpdateChecker>(options);
}

ui::desktop::AppServices AppController::appServices()
{
    ui::desktop::AppServices services;
    services.logFolder = log.getFolder();
    const auto executable = juce::File::getSpecialLocation(juce::File::currentExecutableFile);
#if JUCE_MAC
    const auto bundle = executable.getParentDirectory().getParentDirectory().getParentDirectory();
    services.licencesFolder = bundle.getChildFile("Contents/Resources/Licences");
    const auto team = services::UpdateInstaller::signingTeam(bundle);
    services.signature = team.isNotEmpty() ? "Signed with Developer ID (team " + team + ") and notarised by Apple"
                                           : juce::String("Not signed");
#else
    services.licencesFolder = executable.getSiblingFile("Licences");
#endif
    services.settings = &settings;
    // Copies of co-op sessions: in Documents, unless this run has settings of its own (a
    // second copy of the app on the same computer, a release check).
    services.coopFolder = runOptions.settingsFolder != juce::File()
                              ? settings.getFolder().getChildFile("Co-op")
                              : juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
                                    .getChildFile(services::buildInfo().productName)
                                    .getChildFile("Co-op");
    services.updates = updates.get();
    services.crashReports = crashReports.get();
    services.updateInstallProblem = services::UpdateInstaller::describeRunningApp().whyNot;
    if (services::updatePublicKey().isEmpty())
        services.updateInstallProblem = "this build cannot check the signature of updates";
    services.installUpdate = [this](std::function<void(const juce::String&)> onProblem) {
        installUpdate(std::move(onProblem));
    };
    return services;
}

void AppController::installUpdate(std::function<void(const juce::String&)> onProblem)
{
    if (session.isRecording())
    {
        onProblem("stop the recording first, then update");
        return;
    }
    const auto status = updates->getStatus();
    if (status.state != services::UpdateChecker::State::ready || updatePreparation != nullptr)
        return;

    // Unsaved changes first; then the update is got ready off the message thread (a disk
    // image takes seconds), and the app quits for the helper to finish it.
    view().askToSaveChanges([this, onProblem, download = status.downloadedFile] {
        juce::Logger::writeToLog("Updates: installing " + download.getFileName());
        auto installer = std::make_shared<services::UpdateInstaller>(services::UpdateInstaller::describeRunningApp());

        struct Preparation final : juce::Thread
        {
            std::function<void()> job;
            Preparation() : juce::Thread("Update preparation") {}
            void run() override { job(); }
        };
        auto preparation = std::make_unique<Preparation>();
        preparation->job = [this, installer, download, onProblem, stillHere = alive] {
            const auto problem = installer->prepare(download);
            juce::MessageManager::callAsync([this, installer, problem, onProblem, stillHere] {
                if (!*stillHere)
                    return;
                if (problem.isEmpty() &&
                    installer->finishAfterQuit(services::UpdateInstaller::currentProcessId(),
                                              runOptions.relaunchArguments))
                {
                    juce::Logger::writeToLog("Updates: ready, quitting for the new version to start");
                    juce::JUCEApplication::getInstance()->quit();
                    return;
                }
                juce::Logger::writeToLog("Updates: cannot install: " + problem);
                updatePreparation.reset();
                onProblem(problem.isNotEmpty() ? problem : juce::String("the update could not be started"));
            });
        };
        preparation->startThread();
        updatePreparation = std::move(preparation);
    });
}

void AppController::finishFirstLaunch(const services::FirstLaunchPages& pages, std::optional<bool> sendCrashReports)
{
    // Whatever the device and the theme are now is the user's choice.
    if (pages.audioDevice)
    {
        settings.setAudioDeviceChosen();
        saveDeviceState();
    }
    if (pages.theme)
        settings.setThemeChosen();
    if (pages.crashReports)
    {
        // Closing the window without an answer is no consent.
        const bool allowed = sendCrashReports.value_or(false);
        settings.setCrashReportConsent(allowed ? services::CrashReportConsent::allowed
                                               : services::CrashReportConsent::refused);
        crashReports->setSendAllowed(allowed);
        juce::Logger::writeToLog(juce::String("First launch: crash reports ") + (allowed ? "allowed" : "refused"));
    }
    settings.save();
}

void AppController::runUpdateTest()
{
    // Check, download, verify, install, report: each step as soon as the last is done.
    using State = services::UpdateChecker::State;
    if (!updateTestStarted)
    {
        updateTestStarted = true;
        updates->check(settings.getUpdatePreferences(), true);
        return;
    }
    const auto status = updates->getStatus();
    const auto report = [this, &status](const juce::String& outcome, const juce::String& problem) {
        auto json = std::make_unique<juce::DynamicObject>();
        json->setProperty("outcome", outcome);
        json->setProperty("problem", problem);
        json->setProperty("found", status.release ? status.release->version.toString() : juce::String());
        runOptions.updateTestResult.replaceWithText(juce::JSON::toString(juce::var(json.release())), false, false,
                                                    "\n");
        juce::Logger::writeToLog("Update test: " + outcome + (problem.isNotEmpty() ? ": " + problem : juce::String()));
    };

    if (status.state == State::available && status.installable)
        updates->download();
    else if (status.state == State::ready && updatePreparation == nullptr)
    {
        report("installing", {});
        installUpdate([report](const juce::String& problem) {
            report("refused", problem);
            juce::JUCEApplication::getInstance()->quit();
        });
    }
    else if (status.state == State::failed || status.state == State::upToDate ||
             (status.state == State::available && !status.installable))
    {
        report(status.state == State::upToDate ? "upToDate" : "refused", status.problem);
        juce::JUCEApplication::getInstance()->quit();
    }
}

void AppController::watchUpdates()
{
    if (runOptions.updateTestResult != juce::File())
    {
        runUpdateTest();
        return;
    }
    if (smokeTest != nullptr)
    {
        if (smokeTest->tick())
        {
            smokeTest.reset();
            juce::JUCEApplication::getInstance()->quit();
        }
        return;
    }

    // The check at launch, once the window has settled.
    ++ticks;
    if (ticks == 5 * updateRateHz && settings.getUpdatePreferences().checkAtLaunch)
        updates->check(settings.getUpdatePreferences(), false);

    if (updates->getRevision() == watchedUpdates)
        return;
    watchedUpdates = updates->getRevision();
    const auto status = updates->getStatus();
    using State = services::UpdateChecker::State;
    if (status.state == State::failed)
        juce::Logger::writeToLog("Updates: " + status.problem);
    if (status.state == State::available && status.release)
    {
        juce::Logger::writeToLog("Updates: " + status.release->version.toString() + " is available");
        // Found at launch: offered once, unless the user asked anyway.
        if (!status.manual && !offeredUpdate)
        {
            offeredUpdate = true;
            view().showUpdatePanel();
        }
    }
}

void AppController::logProject()
{
    const auto file = session.getProjectFile();
    const auto unsaved = session.hasUnsavedChanges();
    if (file != shownProject)
        juce::Logger::writeToLog("Project: " + (file != juce::File() ? file.getFileName()
                                                                      : juce::String("new, not saved")));
    else if (shownUnsaved && !unsaved && file != juce::File())
        juce::Logger::writeToLog("Saved " + file.getFileName());
    shownUnsaved = unsaved;
}

void AppController::openFile(const juce::File& file)
{
    view().openFile(file);
}

void AppController::showForReview(const juce::StringArray& what)
{
    view().showForReview(what);
}

void AppController::requestQuit()
{
    view().askToSaveChanges([] { juce::JUCEApplication::getInstance()->quit(); });
}

void AppController::updateWindowTitle()
{
    juce::String title = applicationName + "  -  ";
    const auto file = session.getProjectFile();
    title << (file != juce::File() ? file.getFileNameWithoutExtension() : juce::String("Untitled"));
    if (session.hasUnsavedChanges())
        title << "  (edited)";

    if (title != shownTitle)
    {
        shownTitle = title;
        window->setName(title);
    }
}

void AppController::saveDeviceState()
{
    if (const auto state = deviceHost.getDeviceManager().createStateXml())
        settings.setAudioDeviceState(*state);
    settings.setMidiInputsSwitchedOff(deviceHost.getMidiInputsSwitchedOff());
    settings.save();
}

void AppController::midiDevicesChanged()
{
    if (const auto names = deviceHost.getMidiInputNames().joinIntoString(", "); names != loggedMidiInputs)
        juce::Logger::writeToLog("MIDI inputs: " + (loggedMidiInputs = names));
    session.setMidiInputDevices(deviceHost.getMidiInputNames(), [this](const juce::String& name) {
        return deviceHost.findMidiInput(name);
    });
}

ui::desktop::MainComponent& AppController::view()
{
    return window->getMainComponent();
}

void AppController::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // The device changed (chosen by the user, or unplugged; or a MIDI input switched on
    // or off): remember the choice, and convert the audio again if the sample rate
    // changed.
    saveDeviceState();
    deviceChanged();
    midiDevicesChanged();
}

void AppController::deviceChanged()
{
    session.setSampleRate(engine.getSampleRate());
    noInputsOnDevice = false;

    auto* device = deviceHost.getDeviceManager().getCurrentAudioDevice();
    const auto description =
        device == nullptr ? juce::String("none")
                          : device->getTypeName() + ", out \"" + device->getName() + "\", " +
                                juce::String(device->getCurrentSampleRate()) + " Hz, " +
                                juce::String(device->getCurrentBufferSizeSamples()) + " samples, latency in " +
                                juce::String(device->getInputLatencyInSamples()) + " out " +
                                juce::String(device->getOutputLatencyInSamples()) +
                                (deviceHost.hasInputs() ? ", inputs open" : "");
    if (description != loggedDevice)
        juce::Logger::writeToLog("Audio device: " + (loggedDevice = description));
    session.setDeviceLatency(device != nullptr ? device->getInputLatencyInSamples() : 0,
                             device != nullptr ? device->getOutputLatencyInSamples() : 0);
    session.setInputChannelNames(device != nullptr && deviceHost.hasInputs()
                                     ? device->getInputChannelNames()
                                     : juce::StringArray());

    // Each device pair and rate has its own measured offset.
    offsetKey = latencyOffsetKey();
    savedOffset = settings.getLatencyOffset(offsetKey);
    session.setLatencyOffset(savedOffset);
}

juce::String AppController::latencyOffsetKey()
{
    auto& manager = deviceHost.getDeviceManager();
    const auto setup = manager.getAudioDeviceSetup();
    return manager.getCurrentAudioDeviceType() + "|" + setup.inputDeviceName + "|" +
           setup.outputDeviceName + "|" + juce::String(engine.getSampleRate());
}

void AppController::openInputsIfNeeded()
{
    using Access = controller::Session::InputAccess;
    if (askingForInputs || noInputsOnDevice || deviceHost.hasInputs() ||
        session.getInputAccess() == Access::denied || !session.needsInputs())
        return;

    askingForInputs = true;
    engine::io::MicrophoneAccess::request([this, stillHere = alive](bool granted) {
        if (!*stillHere)
            return;

        askingForInputs = false;
        session.setInputAccess(granted ? Access::granted : Access::denied);
        if (!granted)
        {
            view().showError("Microphone access is off", controller::microphoneDeniedMessage);
            return;
        }

        if (const auto error = deviceHost.openInputs(); error.isNotEmpty())
        {
            noInputsOnDevice = true;
            view().showError("No audio input", "The inputs could not be opened: " + error +
                                                   "\n\nChoose an input device in File > Audio "
                                                   "Settings.");
        }
    });
}

void AppController::timerCallback()
{
    // Scan results first: plugins missing until now may be found.
    if (plugins.update())
        session.pluginsChanged();
    logPlugins();
    if (coop != nullptr)
        coop->update(); // changes of others reach the session before it builds the engine's
    session.update();

    logProject();

    // The project to offer again if a plugin crashes the app.
    if (const auto project = session.getProjectFile(); project != shownProject)
    {
        shownProject = project;
        settings.setLastProject(project);
        settings.save();
    }
    openInputsIfNeeded();

    // A new offset (typed in, or measured) is remembered for this device pair and rate.
    if (session.getLatencyOffset() != savedOffset)
    {
        savedOffset = session.getLatencyOffset();
        settings.setLatencyOffset(offsetKey, savedOffset);
        settings.save();
    }

    updateWindowTitle();
    watchUpdates();
}

} // namespace daw::app
