#include "daw/ui/desktop/MainComponent.h"

#include "daw/model/ProjectFile.h"
#include "daw/ui/desktop/ExportDialog.h"

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
} // namespace

MainComponent::MainComponent(controller::Session& sessionToShow,
                             juce::AudioDeviceManager& audioDeviceManager,
                             juce::ApplicationCommandManager& applicationCommandManager)
    : session(sessionToShow)
    , deviceManager(audioDeviceManager)
    , commandManager(applicationCommandManager)
    , transportBar(sessionToShow)
    , arrangeView(sessionToShow)
    , mixerView(sessionToShow)
{
    addAndMakeVisible(transportBar);
    addAndMakeVisible(arrangeView);
    masterMeter.setTooltip("Master output level (click to clear the clip indicators)");
    addAndMakeVisible(masterMeter);
    addAndMakeVisible(mixerView);

    setSize(1280, 760);
    lastRefreshSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    startTimerHz(refreshRateHz);
}

MainComponent::~MainComponent()
{
    stopTimer();
}

void MainComponent::openFile(const juce::File& file)
{
    if (isProjectFile(file))
        askToSaveChanges([this, file] { openProjectFile(file); });
    else
        session.importAudio({file}, arrangeView.getInsertPosition());
}

void MainComponent::showError(const juce::String& title, const juce::String& message)
{
    juce::NativeMessageBox::showAsync(
        juce::MessageBoxOptions::makeOptionsOk(juce::MessageBoxIconType::WarningIcon, title,
                                               message, "OK", this),
        [](int) {});
}

void MainComponent::askToSaveChanges(std::function<void()> next)
{
    if (!session.hasUnsavedChanges())
    {
        next();
        return;
    }

    const auto file = session.getProjectFile();
    const auto name = file != juce::File() ? file.getFileNameWithoutExtension()
                                           : juce::String("Untitled");
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle("Save changes?")
                             .withMessage("Do you want to save the changes to \"" + name + "\"?")
                             .withButton("Save")
                             .withButton("Don't Save")
                             .withButton("Cancel")
                             .withAssociatedComponent(this);

    juce::NativeMessageBox::showAsync(options, [this, next = std::move(next)](int button) {
        if (button == 0)
            saveThen(next);
        else if (button == 1)
            next();
    });
}

void MainComponent::saveThen(std::function<void()> afterSaving)
{
    if (session.getProjectFile() == juce::File())
    {
        chooseProjectToSave(std::move(afterSaving));
        return;
    }

    if (const auto result = session.saveProject(session.getProjectFile()); result.failed())
        showError("Could not save the project", result.getErrorMessage());
    else if (afterSaving)
        afterSaving();
}

void MainComponent::resized()
{
    auto area = getLocalBounds().reduced(8);

    transportBar.setBounds(area.removeFromTop(30));
    area.removeFromTop(8);

    mixerView.setVisible(mixerVisible);
    if (mixerVisible)
    {
        mixerView.setBounds(area.removeFromBottom(MixerView::preferredHeight));
        area.removeFromBottom(8);
    }

    masterMeter.setBounds(area.removeFromRight(40));
    area.removeFromRight(8);
    arrangeView.setBounds(area);
}

void MainComponent::paint(juce::Graphics& g)
{
    g.fillAll(getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId));
}

void MainComponent::timerCallback()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    const double elapsed = now - lastRefreshSeconds;
    lastRefreshSeconds = now;

    transportBar.refresh();
    arrangeView.refresh();

    // The output peaks can be taken once: both master meters get the same reading.
    auto& engine = session.getEngine();
    const float left = engine.takeOutputPeak(0);
    const float right = engine.takeOutputPeak(1);
    masterMeter.update(left, right, elapsed);
    if (mixerVisible)
        mixerView.refresh(left, right, elapsed);

    // Menu items show play/pause state, ticks and what can be undone.
    const auto state = session.getTransportState();
    const bool selection = arrangeView.hasSelection();
    if (state != lastState || session.getRevision() != lastRevision || selection != lastSelection)
    {
        lastState = state;
        lastRevision = session.getRevision();
        lastSelection = selection;
        commandManager.commandStatusChanged();
    }
}

void MainComponent::togglePlayPause()
{
    if (session.getTransportState() == engine::TransportState::playing)
        session.pause();
    else
        session.play();
}

void MainComponent::openProjectFile(const juce::File& file)
{
    const auto result = session.openProject(file);
    if (result.failed())
        showError("Could not open the project", result.getErrorMessage());
    else
        arrangeView.zoomToFit();
}

void MainComponent::chooseAudioToImport()
{
    fileChooser = std::make_unique<juce::FileChooser>("Import audio files", juce::File(),
                                                      audioFilePatterns);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::canSelectMultipleItems,
                             [this](const juce::FileChooser& chooser) {
                                 const auto files = chooser.getResults();
                                 if (!files.isEmpty())
                                     session.importAudio(files, arrangeView.getInsertPosition());
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
                                 if (file != juce::File())
                                     openProjectFile(file);
                             });
}

void MainComponent::chooseProjectToSave(std::function<void()> afterSaving)
{
    fileChooser = std::make_unique<juce::FileChooser>("Save the project", session.getProjectFile(),
                                                      projectFilePattern);
    fileChooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                 juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::warnAboutOverwriting,
                             [this, afterSaving](const juce::FileChooser& chooser) {
                                 const auto file = chooser.getResult();
                                 if (file == juce::File())
                                     return;
                                 const auto result = session.saveProject(file);
                                 if (result.failed())
                                     showError("Could not save the project",
                                               result.getErrorMessage());
                                 else if (afterSaving)
                                     afterSaving();
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
    options.dialogBackgroundColour =
        getLookAndFeel().findColour(juce::ResizableWindow::backgroundColourId);
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
    commands.addArray({newProject, openProject, saveProject, saveProjectAs, importAudio, exportMix,
                       audioSettings, undo, redo, newTrack, deleteSelection, playPause, stop,
                       toggleLoop, toggleSnap, zoomIn, zoomOut, zoomToFit, toggleMixer});
}

void MainComponent::getCommandInfo(juce::CommandID commandId, juce::ApplicationCommandInfo& result)
{
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shiftCommand =
        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    const auto& project = session.getProject();
    const bool hasClips = project.getEnd() > 0;

    switch (commandId)
    {
    case newProject:
        result.setInfo("New Project", "Closes this project and starts an empty one", "File", 0);
        result.addDefaultKeypress('n', command);
        break;
    case openProject:
        result.setInfo("Open Project...", "Opens a saved project", "File", 0);
        result.addDefaultKeypress('o', command);
        break;
    case saveProject:
        result.setInfo("Save Project", "Saves the project", "File", 0);
        result.addDefaultKeypress('s', command);
        break;
    case saveProjectAs:
        result.setInfo("Save Project As...", "Saves the project to a new file", "File", 0);
        result.addDefaultKeypress('s', shiftCommand);
        break;
    case importAudio:
        result.setInfo("Import Audio...", "Adds WAV or AIFF files on new tracks, at the playhead",
                       "File", 0);
        result.addDefaultKeypress('i', command);
        break;
    case exportMix:
        result.setInfo("Export Mix...", "Writes the mix to a WAV file", "File", 0);
        result.addDefaultKeypress('e', command);
        result.setActive(hasClips);
        break;
    case audioSettings:
        result.setInfo("Audio Output...", "Chooses the audio output device", "File", 0);
        result.addDefaultKeypress(',', command);
        break;
    case undo:
        result.setInfo(session.canUndo() ? "Undo " + session.getUndoDescription() : "Undo",
                       "Undoes the last change", "Edit", 0);
        result.addDefaultKeypress('z', command);
        result.setActive(session.canUndo());
        break;
    case redo:
        result.setInfo(session.canRedo() ? "Redo " + session.getRedoDescription() : "Redo",
                       "Redoes the last undone change", "Edit", 0);
        result.addDefaultKeypress('z', shiftCommand);
        result.setActive(session.canRedo());
        break;
    case newTrack:
        result.setInfo("New Track", "Adds an empty track", "Track", 0);
        result.addDefaultKeypress('t', command);
        break;
    case deleteSelection:
        result.setInfo("Delete", "Deletes the selected clips", "Edit", 0);
        result.addDefaultKeypress(juce::KeyPress::backspaceKey, 0);
        result.addDefaultKeypress(juce::KeyPress::deleteKey, 0);
        result.setActive(arrangeView.hasSelection());
        break;
    case playPause:
        result.setInfo(session.getTransportState() == engine::TransportState::playing ? "Pause"
                                                                                      : "Play",
                       "Starts or pauses playback", "Transport", 0);
        result.addDefaultKeypress(juce::KeyPress::spaceKey, 0);
        result.setActive(hasClips);
        break;
    case stop:
        result.setInfo("Stop", "Stops and goes back to where playback started (again: to 0)",
                       "Transport", 0);
        result.addDefaultKeypress(juce::KeyPress::returnKey, 0);
        break;
    case toggleLoop:
        result.setInfo("Loop", "Loops the loop region, or the whole project", "Transport", 0);
        result.addDefaultKeypress('l', 0);
        result.setTicked(project.isLoopEnabled());
        break;
    case toggleSnap:
        result.setInfo("Snap to Grid", "Snaps clips and edits to the grid", "Transport", 0);
        result.addDefaultKeypress('n', 0);
        result.setTicked(project.isSnapEnabled());
        break;
    case zoomIn:
        result.setInfo("Zoom In", "Shows less of the timeline, in more detail", "View", 0);
        result.addDefaultKeypress('=', command);
        result.addDefaultKeypress('+', command);
        break;
    case zoomOut:
        result.setInfo("Zoom Out", "Shows more of the timeline", "View", 0);
        result.addDefaultKeypress('-', command);
        break;
    case zoomToFit:
        result.setInfo("Zoom to Fit", "Shows the whole project", "View", 0);
        result.addDefaultKeypress('0', command);
        break;
    case toggleMixer:
        result.setInfo("Mixer", "Shows or hides the mixer", "View", 0);
        result.addDefaultKeypress('x', 0);
        result.setTicked(mixerVisible);
        break;
    default:
        break;
    }
}

bool MainComponent::perform(const InvocationInfo& info)
{
    switch (info.commandID)
    {
    case newProject:
        askToSaveChanges([this] { session.newProject(); });
        break;
    case openProject:
        askToSaveChanges([this] { chooseProjectToOpen(); });
        break;
    case saveProject:
        saveThen({});
        break;
    case saveProjectAs:
        chooseProjectToSave();
        break;
    case importAudio:
        chooseAudioToImport();
        break;
    case exportMix:
        ExportDialog::show(session, this);
        break;
    case audioSettings:
        showAudioSettings();
        break;
    case undo:
        session.undo();
        break;
    case redo:
        session.redo();
        break;
    case newTrack:
        session.addTrack();
        break;
    case deleteSelection:
        arrangeView.deleteSelection();
        break;
    case playPause:
        togglePlayPause();
        break;
    case stop:
        session.stop();
        break;
    case toggleLoop:
        session.setLoopEnabled(!session.getProject().isLoopEnabled());
        break;
    case toggleSnap:
        session.setSnapEnabled(!session.getProject().isSnapEnabled());
        break;
    case zoomIn:
        arrangeView.zoomIn();
        break;
    case zoomOut:
        arrangeView.zoomOut();
        break;
    case zoomToFit:
        arrangeView.zoomToFit();
        break;
    case toggleMixer:
        mixerVisible = !mixerVisible;
        resized();
        commandManager.commandStatusChanged();
        break;
    default:
        return false;
    }

    return true;
}

bool MainComponent::isInterestedInFileDrag(const juce::StringArray& files)
{
    return files.size() == 1 && isProjectFile(juce::File(files[0]));
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    openFile(juce::File(files[0]));
}

} // namespace daw::ui::desktop
