#include "AppController.h"

#include "daw/engine/io/MicrophoneAccess.h"
#include "daw/ui/desktop/Alerts.h"

#include <juce_audio_processors/juce_audio_processors.h>

namespace daw::app
{

namespace
{
constexpr const char* deviceStateKey = "audioDevice";
constexpr const char* midiInputsOffKey = "midiInputsOff";
constexpr const char* appearanceKey = "appearance";
constexpr const char* pluginWindowsKey = "pluginWindows";
constexpr const char* lastProjectKey = "lastProject";
constexpr int updateRateHz = 60;
} // namespace

juce::PropertiesFile::Options AppController::settingsOptions(const juce::String& name)
{
    juce::PropertiesFile::Options options;
    options.applicationName = name;
    options.filenameSuffix = ".settings";
    options.folderName = name;
    options.osxLibrarySubFolder = "Application Support";
    return options;
}

controller::PluginLibrary::Options AppController::libraryOptions(const juce::String& name)
{
    const auto folder = settingsOptions(name).getDefaultFile().getParentDirectory();
    folder.createDirectory();
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

AppController::AppController(const juce::String& name)
    : applicationName(name)
    , crashMarker(settingsOptions(name).getDefaultFile().getSiblingFile("CrashedPlugins.txt"))
    , plugins(libraryOptions(name))
{
    settings.setStorageParameters(settingsOptions(applicationName));

    // A plugin that crashed the app last time stays off until the user loads it.
    const auto crashed = crashMarker.takeCrashedPlugins();
    session.holdBackPlugins(crashed);

    // The theme applies before the first window opens, so nothing flashes in another.
    themes = std::make_unique<ui::desktop::ThemeManager>(
        appearance::AppearancePreferences::fromJson(
            settings.getUserSettings()->getValue(appearanceKey)),
        [this](const appearance::AppearancePreferences& preferences)
        {
            settings.getUserSettings()->setValue(appearanceKey, preferences.toJson());
            settings.saveIfNeeded();
        });

    // Inputs open at launch only if the user already allowed them: never a prompt.
    using engine::io::MicrophoneAccess;
    const auto access = MicrophoneAccess::getStatus();
    session.setInputAccess(access == MicrophoneAccess::Status::granted
                               ? controller::Session::InputAccess::granted
                           : access == MicrophoneAccess::Status::denied
                               ? controller::Session::InputAccess::denied
                               : controller::Session::InputAccess::unknown);

    const auto savedDevice = settings.getUserSettings()->getXmlValue(deviceStateKey);
    const auto deviceError =
        deviceHost.initialise(savedDevice.get(), access == MicrophoneAccess::Status::granted);
    deviceHost.getDeviceManager().addChangeListener(this);
    deviceChanged();

    // Every MIDI input is listened to, also those plugged in later, unless the user
    // switched it off in the device settings.
    deviceHost.onMidiDevicesChanged = [this] { midiDevicesChanged(); };
    deviceHost.openMidiInputs(juce::StringArray::fromLines(
        settings.getUserSettings()->getValue(midiInputsOffKey)));
    midiDevicesChanged();

    window = std::make_unique<ui::desktop::MainWindow>(applicationName, session,
                                                       deviceHost.getDeviceManager(), *themes, &plugins);

    auto& positions = view().getPluginWindowPositions();
    positions = ui::desktop::WindowPositions::fromJson(
        juce::JSON::parse(settings.getUserSettings()->getValue(pluginWindowsKey)));
    positions.onChange = [this] {
        settings.getUserSettings()->setValue(pluginWindowsKey,
                                             juce::JSON::toString(view().getPluginWindowPositions().toJson(), true));
        settings.saveIfNeeded();
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
    const juce::File project(settings.getUserSettings()->getValue(lastProjectKey));
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
        settings.getUserSettings()->setValue(deviceStateKey, state.get());
    settings.getUserSettings()->setValue(midiInputsOffKey,
                                         deviceHost.getMidiInputsSwitchedOff().joinIntoString("\n"));
    settings.saveIfNeeded();
}

void AppController::midiDevicesChanged()
{
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
    session.setDeviceLatency(device != nullptr ? device->getInputLatencyInSamples() : 0,
                             device != nullptr ? device->getOutputLatencyInSamples() : 0);
    session.setInputChannelNames(device != nullptr && deviceHost.hasInputs()
                                     ? device->getInputChannelNames()
                                     : juce::StringArray());

    // Each device pair and rate has its own measured offset.
    offsetKey = latencyOffsetKey();
    savedOffset = settings.getUserSettings()->getIntValue(offsetKey, 0);
    session.setLatencyOffset(savedOffset);
}

juce::String AppController::latencyOffsetKey()
{
    auto& manager = deviceHost.getDeviceManager();
    const auto setup = manager.getAudioDeviceSetup();
    return "latencyOffset " + manager.getCurrentAudioDeviceType() + "|" + setup.inputDeviceName +
           "|" + setup.outputDeviceName + "|" + juce::String(engine.getSampleRate());
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
    session.update();

    // The project to offer again if a plugin crashes the app.
    if (const auto project = session.getProjectFile(); project != shownProject)
    {
        shownProject = project;
        settings.getUserSettings()->setValue(lastProjectKey, project.getFullPathName());
        settings.saveIfNeeded();
    }
    openInputsIfNeeded();

    // A new offset (typed in, or measured) is remembered for this device pair and rate.
    if (session.getLatencyOffset() != savedOffset)
    {
        savedOffset = session.getLatencyOffset();
        settings.getUserSettings()->setValue(offsetKey, savedOffset);
        settings.saveIfNeeded();
    }

    updateWindowTitle();
}

} // namespace daw::app
