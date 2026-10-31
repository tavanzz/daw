#include "AppController.h"

#include "daw/engine/io/MicrophoneAccess.h"

namespace daw::app
{

namespace
{
constexpr const char* deviceStateKey = "audioDevice";
constexpr int updateRateHz = 60;
} // namespace

AppController::AppController(const juce::String& name)
    : applicationName(name)
{
    juce::PropertiesFile::Options options;
    options.applicationName = applicationName;
    options.filenameSuffix = ".settings";
    options.folderName = applicationName;
    options.osxLibrarySubFolder = "Application Support";
    settings.setStorageParameters(options);

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

    window = std::make_unique<ui::desktop::MainWindow>(applicationName, session,
                                                       deviceHost.getDeviceManager());

    if (deviceError.isNotEmpty())
        view().showError("No audio output", "The audio output could not be opened: " +
                                                deviceError +
                                                "\n\nChoose another one in File > Audio Settings.");

    updateWindowTitle();
    startTimerHz(updateRateHz);
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
    {
        settings.getUserSettings()->setValue(deviceStateKey, state.get());
        settings.saveIfNeeded();
    }
}

ui::desktop::MainComponent& AppController::view()
{
    return window->getMainComponent();
}

void AppController::changeListenerCallback(juce::ChangeBroadcaster*)
{
    // The device changed (chosen by the user, or unplugged): remember the choice, and
    // convert the audio again if the sample rate changed.
    saveDeviceState();
    deviceChanged();
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
    session.update();
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
