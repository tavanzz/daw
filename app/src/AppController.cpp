#include "AppController.h"

#include "daw/engine/io/MicrophoneAccess.h"
#include "daw/services/BuildInfo.h"
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

services::AppLog::Options AppController::logOptions(const juce::File& settingsFolder) const
{
    // A run with settings of its own keeps its logs with them.
    services::AppLog::Options options;
    options.applicationName = applicationName;
    options.folder = settingsFolder != juce::File() ? settingsFolder.getChildFile("Logs")
                                                    : services::AppLog::defaultFolder(applicationName);
    return options;
}

AppController::AppController(const juce::String& name, const juce::File& settingsFolder)
    : applicationName(name)
    , settings(settingsFolder != juce::File() ? settingsFolder : services::UserSettings::defaultFolder(name), name)
    , log(logOptions(settingsFolder))
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

    const auto savedDevice = settings.getAudioDeviceState();
    const auto deviceError =
        deviceHost.initialise(savedDevice.get(), access == MicrophoneAccess::Status::granted);
    deviceHost.getDeviceManager().addChangeListener(this);
    deviceChanged();

    // Every MIDI input is listened to, also those plugged in later, unless the user
    // switched it off in the device settings.
    deviceHost.onMidiDevicesChanged = [this] { midiDevicesChanged(); };
    deviceHost.openMidiInputs(settings.getMidiInputsSwitchedOff());
    midiDevicesChanged();

    window = std::make_unique<ui::desktop::MainWindow>(applicationName, session,
                                                       deviceHost.getDeviceManager(), *themes, &plugins);
    view().setAppServices({log.getFolder()});

    auto& positions = view().getPluginWindowPositions();
    positions = ui::desktop::WindowPositions::fromJson(settings.getPluginWindows());
    positions.onChange = [this] {
        settings.setPluginWindows(view().getPluginWindowPositions().toJson());
        settings.save();
    };

    // New and changed plugins are found in the background.
    plugins.scan();

    if (deviceError.isNotEmpty())
        view().showError("No audio output", "The audio output could not be opened: " +
                                                deviceError +
                                                "\n\nChoose another one in File > Audio Settings.");

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
    saveDeviceState();
    deviceHost.getDeviceManager().removeChangeListener(this);
    window.reset();
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
}

} // namespace daw::app
