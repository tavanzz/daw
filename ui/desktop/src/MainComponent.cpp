#include "daw/ui/desktop/MainComponent.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace daw::ui::desktop
{

namespace
{
constexpr int refreshRateHz = 60;
constexpr const char* audioFilePatterns = "*.wav;*.wave;*.aif;*.aiff;*.aifc";
constexpr const char* projectFilePattern = "*.dawproject";

bool isProjectFile(const juce::File& file)
{
    return file.hasFileExtension("dawproject");
}

bool isAudioFile(const juce::File& file)
{
    return file.hasFileExtension("wav;wave;aif;aiff;aifc");
}
} // namespace

MainComponent::MainComponent(engine::Engine& engineToShow,
                             juce::AudioDeviceManager& audioDeviceManager,
                             juce::ApplicationCommandManager& applicationCommandManager)
    : engine(engineToShow)
    , deviceManager(audioDeviceManager)
    , commandManager(applicationCommandManager)
{
    transportBar.onPlay = [this] { engine.play(); };
    transportBar.onPause = [this] { engine.pause(); };
    transportBar.onStop = [this] { engine.stop(); };
    transportBar.onLoopToggled = [this](bool shouldLoop) {
        if (onLoopToggled)
            onLoopToggled(shouldLoop);
    };
    addAndMakeVisible(transportBar);

    fileLabel.setFont(juce::FontOptions(16.0f, juce::Font::bold));
    addAndMakeVisible(fileLabel);
    detailsLabel.setColour(juce::Label::textColourId, juce::Colours::grey);
    detailsLabel.setJustificationType(juce::Justification::centredRight);
    addAndMakeVisible(detailsLabel);

    waveform.onSeek = [this](juce::int64 position) { engine.seek(position); };
    waveform.showMessage(emptyMessage());
    addAndMakeVisible(waveform);

    addAndMakeVisible(meter);

    setSize(960, 540);
    lastRefreshSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    startTimerHz(refreshRateHz);
}

MainComponent::~MainComponent()
{
    stopTimer();
}

juce::String MainComponent::emptyMessage()
{
    return "Drop a WAV or AIFF file here, or choose File > Open Audio File...";
}

void MainComponent::showMessage(const juce::String& text)
{
    hasAudio = false;
    fileLabel.setText({}, juce::dontSendNotification);
    detailsLabel.setText({}, juce::dontSendNotification);
    waveform.showMessage(text);
    commandManager.commandStatusChanged();
}

void MainComponent::showAudio(const juce::String& fileName, const juce::String& details,
                              std::shared_ptr<const engine::dsp::WaveformPeaks> peaks)
{
    hasAudio = true;
    fileLabel.setText(fileName, juce::dontSendNotification);
    detailsLabel.setText(details, juce::dontSendNotification);
    waveform.setPeaks(std::move(peaks));
    commandManager.commandStatusChanged();
}

void MainComponent::showError(const juce::String& title, const juce::String& message)
{
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::WarningIcon, title, message,
                                               "OK", this),
        [](int) {});
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(12);

    transportBar.setBounds(area.removeFromTop(32));
    area.removeFromTop(10);

    auto info = area.removeFromTop(24);
    detailsLabel.setBounds(info.removeFromRight(info.getWidth() / 2));
    fileLabel.setBounds(info);
    area.removeFromTop(6);

    meter.setBounds(area.removeFromRight(56));
    area.removeFromRight(10);
    waveform.setBounds(area);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));

    if (dragHover)
    {
        g.setColour(juce::Colour(0xff4fc3f7));
        g.drawRect(waveform.getBounds().expanded(3), 2);
    }
}

void MainComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const double elapsed = now - lastRefreshSeconds;
    lastRefreshSeconds = now;

    const auto state = engine.getTransportState();
    const auto position = engine.getPlayheadPosition();
    const bool looping = engine.isLooping();

    transportBar.update(state, position, engine.getLength(), engine.getSampleRate(), looping,
                        hasAudio);
    waveform.setPlayhead(position);
    meter.update(engine.takeOutputPeak(0), engine.takeOutputPeak(1), elapsed);

    // Menu items show play/pause state and the loop tick.
    if (state != lastState || looping != lastLooping)
    {
        lastState = state;
        lastLooping = looping;
        commandManager.commandStatusChanged();
    }
}

void MainComponent::togglePlayPause()
{
    if (engine.getTransportState() == engine::TransportState::playing)
        engine.pause();
    else
        engine.play();
}

void MainComponent::chooseAudioFile()
{
    fileChooser = std::make_unique<juce::FileChooser>("Open an audio file", juce::File(),
                                                      audioFilePatterns);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser) {
                                 const auto file = chooser.getResult();
                                 if (file != juce::File() && onOpenAudioFile)
                                     onOpenAudioFile(file);
                             });
}

void MainComponent::chooseProjectToOpen()
{
    fileChooser = std::make_unique<juce::FileChooser>("Open a project", juce::File(),
                                                      projectFilePattern);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [this](const juce::FileChooser& chooser) {
                                 const auto file = chooser.getResult();
                                 if (file != juce::File() && onOpenProject)
                                     onOpenProject(file);
                             });
}

void MainComponent::chooseProjectToSave()
{
    fileChooser = std::make_unique<juce::FileChooser>("Save the project", juce::File(),
                                                      projectFilePattern);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                 juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::warnAboutOverwriting,
                             [this](const juce::FileChooser& chooser) {
                                 const auto file = chooser.getResult();
                                 if (file != juce::File() && onSaveProjectAs)
                                     onSaveProjectAs(file);
                             });
}

void MainComponent::showAudioSettings()
{
    auto selector = std::make_unique<juce::AudioDeviceSelectorComponent>(
        deviceManager, 0, 0, 1, 2, false, false, true, false);
    selector->setSize(520, 360);

    juce::DialogWindow::LaunchOptions options;
    options.content.setOwned(selector.release());
    options.dialogTitle = "Audio Output";
    options.dialogBackgroundColour = getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
    options.escapeKeyTriggersCloseButton = true;
    options.useNativeTitleBar = true;
    options.resizable = false;
    options.componentToCentreAround = this;
    options.launchAsync();
}

juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget()
{
    // Commands this component does not handle (such as Quit) go to the application.
    return juce::JUCEApplication::getInstance();
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands)
{
    commands.addArray({openAudioFile, openProject, saveProject, saveProjectAs, audioSettings,
                       playPause, stop, toggleLoop});
}

void MainComponent::getCommandInfo(juce::CommandID commandId, juce::ApplicationCommandInfo& result)
{
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shiftCommand = juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;

    switch (commandId)
    {
    case openAudioFile:
        result.setInfo("Open Audio File...", "Loads a WAV or AIFF file", "File", 0);
        result.addDefaultKeypress('o', command);
        break;
    case openProject:
        result.setInfo("Open Project...", "Opens a saved project", "File", 0);
        result.addDefaultKeypress('o', shiftCommand);
        break;
    case saveProject:
        result.setInfo("Save Project", "Saves the project", "File", 0);
        result.addDefaultKeypress('s', command);
        break;
    case saveProjectAs:
        result.setInfo("Save Project As...", "Saves the project to a new file", "File", 0);
        result.addDefaultKeypress('s', shiftCommand);
        break;
    case audioSettings:
        result.setInfo("Audio Output...", "Chooses the audio output device", "File", 0);
        result.addDefaultKeypress(',', command);
        break;
    case playPause:
        result.setInfo(engine.getTransportState() == engine::TransportState::playing ? "Pause" : "Play",
                       "Starts or pauses playback", "Transport", 0);
        result.addDefaultKeypress(juce::KeyPress::spaceKey, 0);
        result.setActive(hasAudio);
        break;
    case stop:
        result.setInfo("Stop", "Stops and returns to the start", "Transport", 0);
        result.addDefaultKeypress(juce::KeyPress::returnKey, 0);
        result.setActive(hasAudio);
        break;
    case toggleLoop:
        result.setInfo("Loop", "Loops the whole file", "Transport", 0);
        result.addDefaultKeypress('l', 0);
        result.setTicked(engine.isLooping());
        break;
    default:
        break;
    }
}

bool MainComponent::perform(const InvocationInfo& info)
{
    switch (info.commandID)
    {
    case openAudioFile:
        chooseAudioFile();
        return true;
    case openProject:
        chooseProjectToOpen();
        return true;
    case saveProject:
        if (!onSaveProject || !onSaveProject())
            chooseProjectToSave();
        return true;
    case saveProjectAs:
        chooseProjectToSave();
        return true;
    case audioSettings:
        showAudioSettings();
        return true;
    case playPause:
        togglePlayPause();
        return true;
    case stop:
        engine.stop();
        return true;
    case toggleLoop:
        if (onLoopToggled)
            onLoopToggled(!engine.isLooping());
        return true;
    default:
        return false;
    }
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    const bool interested = files.size() == 1 &&
                            (isAudioFile(juce::File(files[0])) || isProjectFile(juce::File(files[0])));
    if (interested != dragHover)
    {
        dragHover = interested;
        repaint();
    }
    return interested;
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    dragHover = false;
    repaint();

    const juce::File file(files[0]);
    if (isProjectFile(file))
    {
        if (onOpenProject)
            onOpenProject(file);
    }
    else if (onOpenAudioFile)
    {
        onOpenAudioFile(file);
    }
}

} // namespace daw::ui::desktop
