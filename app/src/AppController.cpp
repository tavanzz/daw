#include "AppController.h"

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

    const auto savedDevice = settings.getUserSettings()->getXmlValue(deviceStateKey);
    const auto deviceError = deviceHost.initialise(savedDevice.get());
    deviceHost.getDeviceManager().addChangeListener(this);
    session.setSampleRate(engine.getSampleRate());

    window = std::make_unique<ui::desktop::MainWindow>(applicationName, session,
                                                       deviceHost.getDeviceManager());

    if (deviceError.isNotEmpty())
        view().showError("No audio output", "The audio output could not be opened: " +
                                                deviceError +
                                                "\n\nChoose another one in File > Audio Output.");

    updateWindowTitle();
    startTimerHz(updateRateHz);
}

AppController::~AppController()
{
    stopTimer();
    saveDeviceState();
    deviceHost.getDeviceManager().removeChangeListener(this);
    window.reset();
}

void AppController::openFile(const juce::File& file)
{
    view().openFile(file);
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
    session.setSampleRate(engine.getSampleRate());
}

void AppController::timerCallback()
{
    session.update();
    updateWindowTitle();
}

} // namespace daw::app
