#include "daw/ui/desktop/MainComponent.h"

#include "daw/model/ProjectFile.h"
#include "daw/ui/desktop/Alerts.h"
#include "daw/ui/desktop/AnimationsPanel.h"
#include "daw/ui/desktop/AudioSettingsDialog.h"
#include "daw/ui/desktop/ExportDialog.h"
#include "daw/ui/desktop/PluginManagerPanel.h"
#include "daw/ui/desktop/ThemePreviewsPanel.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace daw::ui::desktop
{

namespace
{
constexpr int refreshRateHz = 60;
constexpr const char* audioFilePatterns = "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.mid;*.midi";
constexpr const char* projectFilePattern = "*.dawproject";

bool isProjectFile(const juce::File& file)
{
    return file.hasFileExtension("dawproject");
}
} // namespace

MainComponent::MainComponent(controller::Session& sessionToShow,
                             juce::AudioDeviceManager& audioDeviceManager,
                             juce::ApplicationCommandManager& applicationCommandManager,
                             ThemeManager& themeManager, controller::PluginLibrary* libraryToUse)
    : session(sessionToShow)
    , deviceManager(audioDeviceManager)
    , commandManager(applicationCommandManager)
    , themes(themeManager)
    , pluginLibrary(libraryToUse)
    , transportBar(sessionToShow)
    , arrangeView(sessionToShow)
    , mixerView(sessionToShow)
    , pluginBrowser(sessionToShow, libraryToUse)
{
    transportBar.onRecord = [this]
    {
        toggleRecording();
    };
    addAndMakeVisible(transportBar);
    addAndMakeVisible(hazardBand);
    addAndMakeVisible(arrangeView);
    masterMeter.setTooltip("Master output level (click to clear the clip indicators)");
    addAndMakeVisible(masterMeter);
    mixerView.openEffect = [this](model::ObjectId effectId) { effectEditors.open(effectId); };
    addAndMakeVisible(mixerView);
    addChildComponent(pianoRoll);
    arrangeView.onOpenClip = [this](model::ObjectId clipId) { editClip(clipId); };
    arrangeView.onProblem = [this](const juce::String& title, const juce::String& message) {
        showError(title, message);
    };
    arrangeView.onEffectAdded = [this](model::ObjectId effectId) { effectEditors.open(effectId); };
    pluginBrowser.onAdded = [this](model::ObjectId effectId) { effectEditors.open(effectId); };
    pluginBrowser.targetForDoubleClick = [this] {
        // The clip in the piano roll, else the selected clips: their track.
        if (mixerVisible && showingPianoRoll && pianoRoll.getClip() != 0)
            if (const auto location = session.getProject().findClip(pianoRoll.getClip()))
                return controller::BrowserTarget{
                    controller::BrowserTarget::Kind::track,
                    controller::ChainOwner::track(session.getProject().getTrack(location->trackIndex).getId())};
        return arrangeView.getSelectedTrackTarget();
    };
    addChildComponent(pluginBrowser);
    addAndMakeVisible(statusBar);

    mixerSlide.setWantsFrames(false);
    themes.addListener(*this);

    setSize(1280, 900);
    lastRefreshSeconds = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    startTimerHz(refreshRateHz);
}

MainComponent::~MainComponent()
{
    themes.removeListener(*this);
    stopTimer();
}

void MainComponent::openFile(const juce::File& file)
{
    if (isProjectFile(file))
        askToSaveChanges([this, file] { openProjectFile(file); });
    else if (file.hasFileExtension("mid;midi;smf"))
        session.importMidi(file, arrangeView.getInsertPosition());
    else
        session.importAudio({file}, arrangeView.getInsertPosition());
}

void MainComponent::showForReview(const juce::StringArray& what)
{
    if (what.contains("previews"))
        perform(InvocationInfo(showThemePreviews));
    if (what.contains("animations"))
        perform(InvocationInfo(showAnimations));
    if (what.contains("mixer"))
        showMixer(true);
    if (what.contains("browser"))
        showPluginBrowser(true);
    if (what.contains("plugins"))
        perform(InvocationInfo(showPluginManager));

    if (what.contains("editors"))
    {
        const auto& project = session.getProject();
        for (int index = 0; index < project.getNumTracks(); ++index)
        {
            const auto effects =
                session.getEffects(controller::ChainOwner::track(project.getTrack(index).getId()));
            for (int effect = 0; effect < effects.getNumEffects(); ++effect)
                effectEditors.open(effects.getEffect(effect).getId());
            if (effects.getNumEffects() > 0)
                break;
        }
    }
}

void MainComponent::setAppServices(AppServices servicesToUse)
{
    appServices = std::move(servicesToUse);
    commandManager.commandStatusChanged();
}

void MainComponent::showError(const juce::String& title, const juce::String& message)
{
    juce::Logger::writeToLog("Alert: " + title + ". " + message.replace("\n", " "));
    showAlert(
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

    showAlert(options, [this, next = std::move(next)](int button) {
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
    auto area = getLocalBounds();
    transportBar.setBounds(area.removeFromTop(TransportBar::height));
    hazardBand.setBounds(area.removeFromTop(ThemedLookAndFeel::get(*this).hazardBandHeight()));
    statusBar.setBounds(area.removeFromBottom(StatusBar::height));
    area.reduce(8, 8);

    pluginBrowser.setVisible(browserVisible);
    if (browserVisible)
    {
        pluginBrowser.setBounds(area.removeFromLeft(PluginBrowserPanel::width));
        area.removeFromLeft(8);
    }

    // While it slides in or out, the lower panel (the mixer or the piano roll) takes
    // part of its height.
    const int panelHeight = showingPianoRoll ? std::max(MixerView::preferredHeight, area.getHeight() / 2)
                                             : MixerView::preferredHeight;
    const int shownHeight = juce::roundToInt(static_cast<float>(panelHeight + 8) * mixerAmount);
    juce::Component& panel = showingPianoRoll ? static_cast<juce::Component&>(pianoRoll) : mixerView;
    juce::Component& other = showingPianoRoll ? static_cast<juce::Component&>(mixerView) : pianoRoll;
    other.setVisible(false);
    panel.setVisible(shownHeight > 0);
    if (shownHeight > 0)
    {
        auto panelArea = area.removeFromBottom(shownHeight);
        panel.setBounds(panelArea.withTrimmedTop(8).withHeight(panelHeight));
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
    statusBar.refresh();
    hazardBand.setRecording(session.isRecording());
    arrangeView.refresh();

    // The input peaks are taken once per refresh too; the track headers read them.
    session.takeInputPeaks();
    arrangeView.updateMeters(elapsed);

    if (const auto problem = session.takeRecordingProblem(); problem.isNotEmpty())
        showError("Recording stopped", problem);
    if (session.takeDropouts() > 0)
    {
        juce::Logger::writeToLog("Dropouts: the disk was too slow, some audio was missing");
        statusBar.showWarning("The disk is too slow: some audio was missing");
    }

    // The output peaks can be taken once: both master meters get the same reading.
    auto& engine = session.getEngine();
    const float left = engine.takeOutputPeak(0);
    const float right = engine.takeOutputPeak(1);
    masterMeter.update(left, right, elapsed);
    if (mixerVisible && !showingPianoRoll)
        mixerView.refresh(left, right, elapsed);
    if (mixerVisible && showingPianoRoll)
    {
        pianoRoll.setPlayhead(session.getPlayhead(),
                              session.getTransportState() == engine::TransportState::playing);
        if (session.getRevision() != lastRevision)
            pianoRoll.refresh();
    }
    if (browserVisible)
        pluginBrowser.refresh();
    if (unavailableCheck > 0 && --unavailableCheck == 0)
        reportUnavailablePlugins();

    // Slow plugins are named in the status bar while they are slow.
    if (now - lastSlowWarning >= 0.25)
    {
        lastSlowWarning = now;
        if (const auto slow = session.getPluginLoads().getSlowPlugins(); !slow.isEmpty())
            statusBar.showWarning((slow.size() == 1 ? "Slow plugin: " : "Slow plugins: ") + slow.joinIntoString(", "));
    }
    effectEditors.refresh(elapsed);

    // Menu items show play/pause state, ticks and what can be undone.
    const auto state = session.getTransportState();
    const bool selection = arrangeView.hasSelection();
    const bool recording = session.isRecording();
    if (state != lastState || session.getRevision() != lastRevision || selection != lastSelection ||
        recording != lastRecording)
    {
        lastState = state;
        lastRevision = session.getRevision();
        lastSelection = selection;
        lastRecording = recording;
        commandManager.commandStatusChanged();
    }
}

void MainComponent::lookAndFeelChanged()
{
    resized(); // the band under the transport is the theme's
}

void MainComponent::themeChanged()
{
    commandManager.commandStatusChanged(); // the menu's tick moves
}

void MainComponent::themeAboutToChange()
{
    // The old look fades out over the new one, if panel transitions are on.
    if (!mixerSlide.isActive() || !isShowing())
        return;

    crossfade = std::make_unique<ThemeCrossfade>(
        createComponentSnapshot(getLocalBounds(), true,
                                juce::Component::getApproximateScaleFactorForComponent(this)),
        [this] { crossfade.reset(); });
    crossfade->setBounds(getLocalBounds());
    addAndMakeVisible(*crossfade);
}

void MainComponent::animationSettingsChanged()
{
    commandManager.commandStatusChanged();

    // Effects switched on or off: everything draws again, with or without them.
    repaint();

    // Transitions switched off mid-way end at once.
    if (!mixerSlide.isActive())
    {
        crossfade.reset();
        mixerSlide.setWantsFrames(false);
        mixerAmount = mixerVisible ? 1.0f : 0.0f;
        resized();
    }
}

void MainComponent::showMixer(bool shouldShow)
{
    if (shouldShow && showingPianoRoll && mixerVisible)
    {
        // The piano roll gives way to the mixer at once.
        showingPianoRoll = false;
        resized();
        return;
    }
    if (shouldShow)
        showingPianoRoll = false;
    showLowerPanel(shouldShow);
}

void MainComponent::editClip(model::ObjectId clipId)
{
    pianoRoll.setClip(clipId);
    showPianoRoll(true);
    if (pianoRoll.isShowing())
        pianoRoll.grabKeyboardFocus();
}

void MainComponent::showPianoRoll(bool shouldShow)
{
    if (shouldShow && !showingPianoRoll && mixerVisible)
    {
        showingPianoRoll = true;
        resized();
        return;
    }
    if (shouldShow)
        showingPianoRoll = true;
    showLowerPanel(shouldShow);
}

void MainComponent::showLowerPanel(bool shouldShow)
{
    mixerVisible = shouldShow;

    if (!mixerSlide.isActive())
    {
        mixerAmount = mixerVisible ? 1.0f : 0.0f;
        resized();
        return;
    }

    mixerFrom = mixerAmount;
    mixerSlideStart = -1.0;
    mixerSlide.setWantsFrames(true);
}

void MainComponent::mixerFrame(double seconds)
{
    constexpr double slideSeconds = 0.18;
    if (mixerSlideStart < 0.0)
        mixerSlideStart = seconds;

    const auto progress =
        static_cast<float>(std::min(1.0, (seconds - mixerSlideStart) / slideSeconds));
    const float eased = 1.0f - (1.0f - progress) * (1.0f - progress);
    const float target = mixerVisible ? 1.0f : 0.0f;
    mixerAmount = mixerFrom + (target - mixerFrom) * eased;

    if (progress >= 1.0f)
    {
        mixerAmount = target;
        mixerSlide.setWantsFrames(false);
    }

    resized();
}

void MainComponent::togglePlayPause()
{
    if (session.getTransportState() == engine::TransportState::playing)
        session.pause();
    else
        session.play();
}

void MainComponent::openProjectFile(const juce::File& file, bool withPlugins)
{
    effectEditors.closeAll();
    const auto result = session.openProject(file, withPlugins);
    if (result.failed())
    {
        showError("Could not open the project", result.getErrorMessage());
        return;
    }

    arrangeView.zoomToFit();
    offerRecovery();
    // The session looks its plugins up at its next updates.
    unavailableCheck = 3;
}

void MainComponent::reportUnavailablePlugins()
{
    const auto names = session.getUnavailablePlugins();
    if (names.isEmpty())
        return;
    showError(names.size() == 1 ? "A plugin is missing" : "Some plugins are missing",
              "This project uses " + names.joinIntoString(", ") +
                  (names.size() == 1 ? ", which is" : ", which are") +
                  " not installed here or could not be loaded. Their settings are kept and saved "
                  "as they are; effects pass the sound through, instruments are silent.");
}

void MainComponent::showPluginBrowser(bool shouldShow)
{
    browserVisible = shouldShow;
    if (shouldShow)
        pluginBrowser.refresh();
    resized();
    commandManager.commandStatusChanged();
}

void MainComponent::offerRecovery()
{
    const auto count = static_cast<int>(session.getRecoverableRecordings().size());
    if (count == 0)
        return;

    const auto options =
        juce::MessageBoxOptions()
            .withIconType(juce::MessageBoxIconType::QuestionIcon)
            .withTitle("Recover recordings?")
            .withMessage(juce::String(count) +
                         (count == 1 ? " recording was" : " recordings were") +
                         " made in this project but not saved in it: the app may have quit "
                         "unexpectedly while recording. Recover them as clips, or delete "
                         "their files?")
            .withButton("Recover")
            .withButton("Delete")
            .withButton("Not Now")
            .withAssociatedComponent(this);

    showAlert(options, [this](int button) {
        if (button == 0)
        {
            if (const auto recovered = session.recoverRecordings(); recovered.failed())
                showError("Some recordings could not be recovered", recovered.getErrorMessage());
        }
        else if (button == 1)
        {
            session.discardRecoverableRecordings();
        }
    });
}

void MainComponent::chooseAudioToImport()
{
    fileChooser = std::make_unique<juce::FileChooser>("Import audio or MIDI files", juce::File(),
                                                      audioFilePatterns);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles |
                                 juce::FileBrowserComponent::canSelectMultipleItems,
                             [this](const juce::FileChooser& chooser) {
                                 juce::Array<juce::File> audio;
                                 for (const auto& file : chooser.getResults())
                                 {
                                     if (!file.hasFileExtension("mid;midi;smf"))
                                     {
                                         audio.add(file);
                                         continue;
                                     }
                                     const auto import =
                                         session.importMidi(file, arrangeView.getInsertPosition());
                                     if (import.result.failed())
                                         showError("The MIDI file could not be imported",
                                                   import.result.getErrorMessage());
                                     else if (!import.leftOut.isEmpty())
                                         showError("Imported, with some things left out",
                                                   file.getFileName() + " had " +
                                                       import.leftOut.joinIntoString(", ") +
                                                       ", which this version does not keep.");
                                 }
                                 if (!audio.isEmpty())
                                     session.importAudio(audio, arrangeView.getInsertPosition());
                             });
}

void MainComponent::chooseMidiExport()
{
    // Type 1 (a track per MIDI track) unless the user asks for one track (type 0).
    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle("Export MIDI File")
                             .withMessage("Write each MIDI track as a track of its own (type 1), "
                                          "or everything in one track (type 0)?")
                             .withButton("Type 1")
                             .withButton("Type 0")
                             .withButton("Cancel")
                             .withAssociatedComponent(this);
    showAlert(options, [this](int button) {
        if (button == 2)
            return; // Cancel, or Escape
        const int type = button == 0 ? 1 : 0;
        fileChooser = std::make_unique<juce::FileChooser>("Export MIDI File", juce::File(), "*.mid");
        fileChooser->launchAsync(juce::FileBrowserComponent::saveMode |
                                     juce::FileBrowserComponent::warnAboutOverwriting,
                                 [this, type](const juce::FileChooser& chooser) {
                                     auto file = chooser.getResult();
                                     if (file == juce::File())
                                         return;
                                     if (!file.hasFileExtension("mid"))
                                         file = file.withFileExtension("mid");
                                     if (const auto result = session.exportMidi(file, type); result.failed())
                                         showError("Could not export", result.getErrorMessage());
                                 });
    });
}

void MainComponent::chooseProjectToOpen(bool withPlugins)
{
    fileChooser = std::make_unique<juce::FileChooser>(
        withPlugins ? "Open a project" : "Open a project without its plugins", juce::File(), projectFilePattern);
    fileChooser->launchAsync(juce::FileBrowserComponent::openMode |
                                 juce::FileBrowserComponent::canSelectFiles,
                             [this, withPlugins](const juce::FileChooser& chooser) {
                                 const auto file = chooser.getResult();
                                 if (file != juce::File())
                                     openProjectFile(file, withPlugins);
                             });
}

void MainComponent::toggleRecording()
{
    if (session.isRecording())
    {
        session.stop();
        return;
    }

    // Recordings go into the project's folder: a project never saved is saved first.
    if (session.getProjectFile() == juce::File())
        chooseProjectToSave([this] { startRecording(); }, "Save the project before recording");
    else
        startRecording();
}

void MainComponent::startRecording()
{
    if (const auto result = session.record(); result.failed())
        showError("Cannot record", result.getErrorMessage());
}

void MainComponent::confirmDiscardRecording()
{
    if (!session.isRecording())
        return;

    const auto options = juce::MessageBoxOptions()
                             .withIconType(juce::MessageBoxIconType::QuestionIcon)
                             .withTitle("Discard the recording?")
                             .withMessage("Discard the recording and delete its files?")
                             .withButton("Discard")
                             .withButton("Keep Recording")
                             .withAssociatedComponent(this);

    showAlert(options,
                                      [this](int button)
                                      {
                                          if (button == 0)
                                              session.discardRecording();
                                      });
}

void MainComponent::chooseProjectToSave(std::function<void()> afterSaving,
                                        const juce::String& title)
{
    fileChooser =
        std::make_unique<juce::FileChooser>(title, session.getProjectFile(), projectFilePattern);
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
    AudioSettingsDialog::show(session, deviceManager, this);
}

juce::ApplicationCommandTarget* MainComponent::getNextCommandTarget()
{
    // Commands this component does not handle (such as Quit) go to the application.
    return juce::JUCEApplication::getInstance();
}

void MainComponent::getAllCommands(juce::Array<juce::CommandID>& commands)
{
    commands.addArray({newProject,       openProject,     saveProject,   saveProjectAs, importAudio,
                       exportMix,        audioSettings,   undo,          redo,          newTrack,
                       deleteSelection,  playPause,       stop,          toggleLoop,    toggleSnap,
                       zoomIn,           zoomOut,         zoomToFit,     toggleMixer,   record,
                       discardRecording, toggleMetronome, toggleCountIn, togglePunch,
                       newBus,           toggleAutomation, showThemePreviews, showAnimations,
                       toggleAllAnimations, newMidiTrack, duplicate, togglePianoRoll,
                       quantiseNotes,    exportMidi,      panic, togglePluginBrowser, openWithoutPlugins, showPluginManager,
                       openLogFolder});

    const auto numThemes = static_cast<int>(themes.getLibrary().getThemes().size());
    for (int index = 0; index < std::min(numThemes, lastTheme - firstTheme + 1); ++index)
        commands.add(firstTheme + index);
}

void MainComponent::getCommandInfo(juce::CommandID commandId, juce::ApplicationCommandInfo& result)
{
    const auto command = juce::ModifierKeys::commandModifier;
    const auto shiftCommand =
        juce::ModifierKeys::commandModifier | juce::ModifierKeys::shiftModifier;
    const auto& project = session.getProject();
    const bool hasClips = project.getEnd() > 0;
    const bool recording = session.isRecording();

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
        result.setInfo("Import Audio or MIDI...",
                       "Adds WAV, AIFF or MIDI files on new tracks, at the playhead",
                       "File", 0);
        result.addDefaultKeypress('i', command);
        break;
    case exportMix:
        result.setInfo("Export Mix...", "Writes the mix to a WAV file", "File", 0);
        result.addDefaultKeypress('e', command);
        result.setActive(hasClips);
        break;
    case audioSettings:
        result.setInfo("Audio Settings...",
                       "Chooses the audio devices and MIDI inputs, and sets up recording", "File", 0);
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
    case newBus:
        result.setInfo("New Bus", "Adds an aux bus for tracks to send to", "Track", 0);
        result.addDefaultKeypress('t', shiftCommand);
        break;
    case toggleAutomation:
        result.setInfo("Automation", "Shows or hides the automation over the tracks", "View", 0);
        result.addDefaultKeypress('a', 0);
        result.setTicked(arrangeView.isShowingAutomation());
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
        result.setTicked(mixerVisible && !showingPianoRoll);
        break;
    case record:
        result.setInfo(recording ? "Stop Recording" : "Record",
                       "Records the armed tracks from the playhead", "Transport", 0);
        result.addDefaultKeypress('r', 0);
        break;
    case discardRecording:
        result.setInfo("Discard Recording...", "Stops recording and deletes what was recorded",
                       "Transport", 0);
        result.addDefaultKeypress('.', command);
        result.setActive(recording);
        break;
    case panic:
        result.setInfo("Panic", "Stops every note and sound of every instrument at once",
                       "Transport", 0);
        result.addDefaultKeypress('.', shiftCommand);
        break;
    case toggleMetronome:
        result.setInfo("Metronome", "Clicks on the beats while playing and recording", "Transport",
                       0);
        result.addDefaultKeypress('k', 0);
        result.setTicked(project.isMetronomeEnabled());
        break;
    case toggleCountIn:
        result.setInfo("Count-in", "Counts in before recording", "Transport", 0);
        result.addDefaultKeypress('k', juce::ModifierKeys::shiftModifier);
        result.setTicked(project.isCountInEnabled());
        break;
    case togglePunch:
        result.setInfo("Punch In/Out", "Records only inside the loop region", "Transport", 0);
        result.addDefaultKeypress('p', 0);
        result.setTicked(project.isPunchEnabled());
        break;
    case showThemePreviews:
        result.setInfo("Show Previews...", "Shows every theme side by side", "View", 0);
        break;
    case newMidiTrack:
        result.setInfo("New MIDI Track", "Adds a MIDI track with the synth", "Track", 0);
        result.addDefaultKeypress('t', command | juce::ModifierKeys::altModifier);
        break;
    case duplicate:
        result.setInfo("Duplicate", "Copies the selected clips (or notes) right after themselves",
                       "Edit", 0);
        result.addDefaultKeypress('d', command);
        result.setActive(arrangeView.hasSelection() || pianoRoll.hasSelection());
        break;
    case togglePianoRoll:
        result.setInfo("Piano Roll", "Shows or hides the piano roll (double-click a MIDI clip)",
                       "View", 0);
        result.addDefaultKeypress('e', 0);
        result.setTicked(mixerVisible && showingPianoRoll);
        break;
    case quantiseNotes:
        result.setInfo("Quantise Notes", "Moves the selected notes (or all) to the grid", "Edit", 0);
        result.addDefaultKeypress('q', 0);
        result.setActive(mixerVisible && showingPianoRoll && pianoRoll.getClip() != 0);
        break;
    case exportMidi:
        result.setInfo("Export MIDI File...", "Writes the MIDI tracks to a standard MIDI file",
                       "File", 0);
        result.addDefaultKeypress('e', command | juce::ModifierKeys::altModifier);
        break;
    case showAnimations:
        result.setInfo("Animations...", "Chooses which animations play, for every theme", "View",
                       0);
        break;
    case togglePluginBrowser:
        result.setInfo("Plugin Browser", "Shows or hides the effects, instruments and plugins to drag onto tracks",
                       "View", 0);
        result.addDefaultKeypress('b', 0);
        result.setTicked(browserVisible);
        break;
    case openWithoutPlugins:
        result.setInfo("Open Without Plugins...",
                       "Opens a project with its plugins switched off (load each from its slot)", "File", 0);
        break;
    case showPluginManager:
        result.setInfo("Plugin Manager...", "Scans for plugins, lists those found and those that failed, "
                       "and the folders searched", "File", 0);
        result.setActive(pluginLibrary != nullptr);
        break;
    case openLogFolder:
        result.setInfo("Open Log Folder", "Shows the folder of the app's logs, to send them when "
                       "something went wrong", "Help", 0);
        result.setActive(appServices.logFolder != juce::File());
        break;
    case toggleAllAnimations:
        result.setInfo("All Animations Off", "Switches every animation off, and again back on",
                       "View", 0);
        result.addDefaultKeypress('a', shiftCommand);
        result.setTicked(themes.getPreferences().animations.level ==
                         appearance::AnimationLevel::off);
        break;
    default:
        if (commandId >= firstTheme && commandId <= lastTheme)
        {
            const auto& library = themes.getLibrary().getThemes();
            const auto index = static_cast<std::size_t>(commandId - firstTheme);
            if (index < library.size())
            {
                const auto& theme = library[index];
                result.setInfo(theme.name, "Uses the " + theme.name + " theme", "View", 0);
                result.addDefaultKeypress(static_cast<juce::juce_wchar>('1' + index), command);
                result.setTicked(themes.getTheme().id == theme.id);
            }
        }
        break;
    }

    // While recording the project stays as it is: no other project, no edits that
    // could remove a track being recorded, no saving half a recording.
    switch (commandId)
    {
    case newProject:
    case openProject:
    case openWithoutPlugins:
    case saveProject:
    case saveProjectAs:
    case importAudio:
    case exportMix:
    case undo:
    case redo:
    case newTrack:
    case newBus:
    case newMidiTrack:
    case duplicate:
    case deleteSelection:
        if (recording)
            result.setActive(false);
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
    case newMidiTrack:
        session.addMidiTrack();
        break;
    case duplicate:
        if (pianoRoll.isShowing() && pianoRoll.hasKeyboardFocus(true))
            pianoRoll.duplicateSelection();
        else
            arrangeView.duplicateSelection();
        break;
    case togglePianoRoll:
        showPianoRoll(!(mixerVisible && showingPianoRoll));
        break;
    case quantiseNotes:
        pianoRoll.quantise();
        break;
    case exportMidi:
        chooseMidiExport();
        break;
    case newBus:
        session.addBus();
        if (!mixerVisible)
            perform(InvocationInfo(toggleMixer));
        break;
    case toggleAutomation:
        arrangeView.setShowingAutomation(!arrangeView.isShowingAutomation());
        commandManager.commandStatusChanged();
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
        // With the piano roll showing, X brings the mixer in its place.
        showMixer(!(mixerVisible && !showingPianoRoll));
        commandManager.commandStatusChanged();
        break;
    case record:
        toggleRecording();
        break;
    case discardRecording:
        confirmDiscardRecording();
        break;
    case panic:
        session.panic();
        break;
    case togglePluginBrowser:
        showPluginBrowser(!browserVisible);
        break;
    case openWithoutPlugins:
        askToSaveChanges([this] { chooseProjectToOpen(false); });
        break;
    case showPluginManager:
        if (pluginLibrary != nullptr)
            showPanel(pluginManagerWindow, "Plugin Manager",
                      [this] { return std::make_unique<PluginManagerPanel>(*pluginLibrary); });
        break;
    case openLogFolder:
        if (appServices.logFolder.isDirectory())
            appServices.logFolder.startAsProcess();
        break;
    case toggleMetronome:
        session.setMetronomeEnabled(!session.getProject().isMetronomeEnabled());
        break;
    case toggleCountIn:
        session.setCountInEnabled(!session.getProject().isCountInEnabled());
        break;
    case togglePunch:
        session.setPunchEnabled(!session.getProject().isPunchEnabled());
        break;
    case showThemePreviews:
        showPanel(previewsWindow, "Themes",
                  [this] { return std::make_unique<ThemePreviewsPanel>(themes); });
        break;
    case showAnimations:
        showPanel(animationsWindow, "Animations",
                  [this] { return std::make_unique<AnimationsPanel>(themes); });
        break;
    case toggleAllAnimations:
    {
        auto settings = themes.getPreferences().animations;
        settings.toggleAllOff();
        themes.setAnimationSettings(settings);
        commandManager.commandStatusChanged();
        break;
    }
    default:
    {
        const auto& library = themes.getLibrary().getThemes();
        const auto index = static_cast<std::size_t>(info.commandID - firstTheme);
        if (info.commandID < firstTheme || info.commandID > lastTheme || index >= library.size())
            return false;

        themes.setTheme(library[index].id);
        break;
    }
    }

    return true;
}

void MainComponent::showPanel(std::unique_ptr<PanelWindow>& window, const juce::String& title,
                              const std::function<std::unique_ptr<juce::Component>()>& makeContent)
{
    if (window != nullptr)
    {
        window->toFront(true);
        return;
    }

    window = std::make_unique<PanelWindow>(title, makeContent(), this,
                                           [&window]
                                           {
                                               // Closing from the window's own button: not
                                               // from inside its callback.
                                               juce::MessageManager::callAsync(
                                                   [&window] { window.reset(); });
                                           });
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
