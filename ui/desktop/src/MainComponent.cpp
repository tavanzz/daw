#include "daw/ui/desktop/MainComponent.h"

#include "daw/model/FormatVersion.h"
#include "daw/model/ProjectFile.h"
#include "daw/services/BuildInfo.h"
#include "daw/services/CrashReporting.h"
#include "daw/services/UpdateChecker.h"
#include "daw/services/UserSettings.h"
#include "daw/ui/desktop/AboutPanel.h"
#include "daw/ui/desktop/Alerts.h"
#include "daw/ui/desktop/AnimationsPanel.h"
#include "daw/ui/desktop/AudioSettingsDialog.h"
#include "daw/ui/desktop/ExportDialog.h"
#include "daw/ui/desktop/FirstLaunchPanel.h"
#include "daw/ui/desktop/PluginManagerPanel.h"
#include "daw/ui/desktop/ThemePreviewsPanel.h"
#include "daw/ui/desktop/UpdatePanel.h"

#include <juce_audio_utils/juce_audio_utils.h>

namespace daw::ui::desktop
{

namespace
{
constexpr int refreshRateHz = 60;
constexpr const char* audioFilePatterns = "*.wav;*.wave;*.aif;*.aiff;*.aifc;*.mid;*.midi";
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
    if (model::isProjectFile(file))
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
    if (coop != nullptr && appServices.settings != nullptr)
        coop->setFollowing(appServices.settings->getCoopPreferences().follow);
    commandManager.commandStatusChanged();
}

void MainComponent::setCoopSession(controller::CoopSession* coopToUse)
{
    coop = coopToUse;
    statusBar.setCoop(coop);
    arrangeView.setCoop(coop);
    pianoRoll.setCoop(coop);
    mixerView.setCoop(coop);
    effectEditors.setCoop(coop);
    if (coop != nullptr && appServices.settings != nullptr)
        coop->setFollowing(appServices.settings->getCoopPreferences().follow);
    commandManager.commandStatusChanged();
}

juce::File MainComponent::coopFolder() const
{
    if (appServices.coopFolder != juce::File())
        return appServices.coopFolder;
    return juce::File::getSpecialLocation(juce::File::userDocumentsDirectory)
        .getChildFile(services::buildInfo().productName)
        .getChildFile("Co-op");
}

void MainComponent::showCoopStart(CoopStartPanel::Mode mode, const juce::String& link)
{
    if (coop == nullptr)
        return;
    if (coop->isActive())
    {
        showCoopSession();
        return;
    }
    coopStartWindow.reset(); // a host window may become a join window, with a new link

    const auto preferences = appServices.settings != nullptr ? appServices.settings->getCoopPreferences()
                                                             : services::CoopPreferences{};
    const auto shownLink = link.isNotEmpty() ? link
                           : mode == CoopStartPanel::Mode::host ? preferences.hostLink
                                                                : juce::String();
    auto name = preferences.name;
    if (name.isEmpty())
        name = juce::SystemStats::getFullUserName().upToFirstOccurrenceOf(" ", false, false);

    const juce::Component::SafePointer<MainComponent> self(this);
    CoopStartPanel::Actions actions;
    actions.start = [self, mode](const juce::String& text, const juce::String& who,
                                 std::function<void(juce::Result)> started) {
        if (self != nullptr)
            self->startCoop(mode, text, who, std::move(started));
    };
    actions.connected = [self] {
        juce::MessageManager::callAsync([self] {
            if (self == nullptr)
                return;
            self->coopStartWindow.reset();
            self->showCoopSession();
        });
    };
    actions.giveUp = [self] {
        if (self != nullptr && self->coop != nullptr && self->coop->isActive())
        {
            self->coop->leave();
            juce::Logger::writeToLog("Co-op: gave up connecting");
            self->commandManager.commandStatusChanged();
        }
    };
    actions.close = [self] {
        juce::MessageManager::callAsync([self] {
            if (self != nullptr)
                self->coopStartWindow.reset();
        });
    };
    showPanel(coopStartWindow, mode == CoopStartPanel::Mode::host ? "Host a Session" : "Join a Session",
              [&] { return std::make_unique<CoopStartPanel>(mode, *coop, shownLink, name, actions); });
}

void MainComponent::startCoop(CoopStartPanel::Mode mode, const juce::String& link, const juce::String& name,
                              std::function<void(juce::Result)> started)
{
    if (coop == nullptr || session.isRecording())
    {
        started(juce::Result::fail("Stop recording first."));
        return;
    }

    // Hosting replaces the open project by its co-op copy, joining by the session's: what
    // is not saved would be lost to the original.
    const juce::Component::SafePointer<MainComponent> self(this);
    auto finish = std::make_shared<std::function<void(juce::Result)>>(std::move(started));
    askToSaveChanges([self, mode, link, name, finish] {
        if (self == nullptr || self->coop == nullptr)
            return;
        auto& coopSession = *self->coop;
        juce::Result result = juce::Result::ok();
        if (mode == CoopStartPanel::Mode::host)
        {
            const auto serverLink = controller::parseServerLink(link);
            const auto project = self->session.getProjectFile();
            const auto title = project != juce::File() ? project.getFileNameWithoutExtension() : juce::String("Session");
            result = serverLink ? coopSession.host(*serverLink, name, controller::newCopyFolder(self->coopFolder(), title))
                                : juce::Result::fail("That is not a server link.");
        }
        else
        {
            const auto invite = controller::parseInvite(link);
            result = invite ? coopSession.join(*invite, name,
                                               controller::newCopyFolder(self->coopFolder(), invite->title))
                            : juce::Result::fail("That is not an invite.");
        }

        if (result.wasOk() && self->appServices.settings != nullptr)
        {
            auto preferences = self->appServices.settings->getCoopPreferences();
            preferences.name = name;
            if (mode == CoopStartPanel::Mode::host)
                preferences.hostLink = link;
            self->appServices.settings->setCoopPreferences(preferences);
            self->appServices.settings->save();
        }
        juce::Logger::writeToLog(juce::String(mode == CoopStartPanel::Mode::host ? "Co-op: hosting" : "Co-op: joining")
                                 + (result.wasOk() ? juce::String() : " failed: " + result.getErrorMessage()));
        self->commandManager.commandStatusChanged();
        (*finish)(result);
    });
}

void MainComponent::openCoopLink(const juce::String& link)
{
    if (controller::parseInvite(link))
        showCoopStart(CoopStartPanel::Mode::join, link);
    else if (controller::parseServerLink(link))
        showCoopStart(CoopStartPanel::Mode::host, link);
    else
        showError("Not a co-op link", "This link cannot be opened: " + link.substring(0, 80));
}

juce::Result MainComponent::rejoinCoop(const juce::File& copyFolder)
{
    if (coop == nullptr)
        return juce::Result::fail("Co-op is not available.");
    effectEditors.closeAll();
    const auto result = coop->rejoin(copyFolder);
    juce::Logger::writeToLog(result.wasOk() ? juce::String("Co-op: rejoining the session of a copy")
                                            : "Co-op: cannot rejoin: " + result.getErrorMessage());
    if (result.wasOk())
    {
        arrangeView.zoomToFit();
        commandManager.commandStatusChanged();
    }
    return result;
}

void MainComponent::showCoopSession()
{
    if (coop == nullptr || coop->findClient() == nullptr)
        return;
    const juce::Component::SafePointer<MainComponent> self(this);
    CoopSessionPanel::Actions actions;
    actions.leave = [self] {
        if (self == nullptr)
            return;
        if (self->coop != nullptr && self->coop->isActive())
            self->confirmLeaveCoop();
        else
            juce::MessageManager::callAsync([self] {
                if (self != nullptr)
                    self->coopSessionWindow.reset();
            });
    };
    actions.closeSession = [self] {
        if (self != nullptr)
            self->confirmCloseCoop();
    };
    actions.follow = [self](bool follow) {
        if (self == nullptr || self->appServices.settings == nullptr)
            return;
        auto preferences = self->appServices.settings->getCoopPreferences();
        preferences.follow = follow;
        self->appServices.settings->setCoopPreferences(preferences);
        self->commandManager.commandStatusChanged();
    };
    showPanel(coopSessionWindow, "Co-op Session",
              [&] { return std::make_unique<CoopSessionPanel>(*coop, actions); });
}

CoopStartPanel* MainComponent::findCoopStartPanel() const
{
    return coopStartWindow != nullptr ? dynamic_cast<CoopStartPanel*>(coopStartWindow->getContentComponent())
                                      : nullptr;
}

CoopSessionPanel* MainComponent::findCoopSessionPanel() const
{
    return coopSessionWindow != nullptr ? dynamic_cast<CoopSessionPanel*>(coopSessionWindow->getContentComponent())
                                        : nullptr;
}

void MainComponent::confirmLeaveCoop()
{
    if (coop == nullptr || !coop->isActive())
        return;
    const juce::Component::SafePointer<MainComponent> self(this);
    showAlert(juce::MessageBoxOptions()
                  .withIconType(juce::MessageBoxIconType::QuestionIcon)
                  .withTitle("Leave the co-op session?")
                  .withMessage("Your copy stays in " + coop->getCopyFolder().getFullPathName()
                               + ", as an ordinary project. To come back you need a new invite from the host.")
                  .withButton("Leave")
                  .withButton("Cancel")
                  .withAssociatedComponent(this),
              [self](int button) {
                  if (self == nullptr || button != 0 || self->coop == nullptr)
                      return;
                  self->coop->leave();
                  juce::Logger::writeToLog("Co-op: left the session");
                  self->commandManager.commandStatusChanged();
                  if (auto* panel = self->findCoopSessionPanel())
                      panel->refresh();
              });
}

void MainComponent::confirmCloseCoop()
{
    if (coop == nullptr || !coop->isActive() || !coop->getClient().isHost())
        return;
    const juce::Component::SafePointer<MainComponent> self(this);
    showAlert(juce::MessageBoxOptions()
                  .withIconType(juce::MessageBoxIconType::WarningIcon)
                  .withTitle("Close the session for everyone?")
                  .withMessage("Everyone's copy stays on their computer, as an ordinary project, and the "
                               "server deletes the session and its files. Nobody can join it again.")
                  .withButton("Close Session")
                  .withButton("Cancel")
                  .withAssociatedComponent(this),
              [self](int button) {
                  if (self == nullptr || button != 0 || self->coop == nullptr || !self->coop->isActive())
                      return;
                  self->coop->getClient().sendHostCommand({coop::HostCommand::Kind::close, 0});
                  juce::Logger::writeToLog("Co-op: closing the session");
              });
}

void MainComponent::refreshCoop()
{
    if (coop == nullptr)
        return;

    juce::StringArray notices = coop->takeNotices();
    if (const auto* client = coop->findClient())
    {
        const auto status = client->getStatus();
        if (status != lastCoopStatus)
        {
            juce::Logger::writeToLog("Co-op: " + juce::String(status == coop::Client::Status::connected      ? "connected"
                                                              : status == coop::Client::Status::reconnecting ? "reconnecting"
                                                              : status == coop::Client::Status::ended        ? "ended"
                                                                                                             : "connecting"));
            // The end of a session is worth an alert, unless this copy left on purpose
            // (that ends nothing on screen) or the start window says why it could not join.
            if (status == coop::Client::Status::ended && lastCoopStatus != coop::Client::Status::idle
                && lastCoopStatus != coop::Client::Status::connecting && coopStartWindow == nullptr)
            {
                showError("The co-op session ended", client->getProblem() + "\n\nYour copy stays in "
                                                         + coop->getCopyFolder().getFullPathName()
                                                         + ", as an ordinary project.");
                notices.clear();
            }
            lastCoopStatus = status;
            commandManager.commandStatusChanged();
        }
        if (client->getRevision() != lastCoopRevision)
        {
            lastCoopRevision = client->getRevision();
            commandManager.commandStatusChanged();
            if (mixerVisible && showingPianoRoll)
                pianoRoll.repaint(); // the others' pointers and selections
        }
    }

    // What this participant looks at, for the others (sent when it changes, at most 20
    // times a second).
    if (coop->isActive())
    {
        auto presence = arrangeView.getPresence();
        if (mixerVisible && showingPianoRoll && pianoRoll.getClip() != 0)
        {
            pianoRoll.fillPresence(presence);
            if (const auto location = session.getProject().findClip(pianoRoll.getClip()))
                presence.track = session.getProject().getTrack(location->trackIndex).getId();
        }
        coop->setPresence(std::move(presence));
    }
    if (!notices.isEmpty())
    {
        for (const auto& notice : notices)
            juce::Logger::writeToLog("Co-op notice: " + notice.upToFirstOccurrenceOf("\"", false, false));
        statusBar.showWarning(notices.joinIntoString("  "));
    }
}

void MainComponent::showFirstLaunch(services::FirstLaunchPages pages,
                                    std::function<void(std::optional<bool>)> finished)
{
    if (firstLaunchWindow != nullptr || !pages.any())
        return;

    // Answered once, either by finishing the pages or by closing the window.
    auto answer = std::make_shared<std::function<void(std::optional<bool>)>>(std::move(finished));
    const auto finish = [this, answer](std::optional<bool> sendCrashReports) {
        if (*answer)
            std::exchange(*answer, nullptr)(sendCrashReports);
        juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
            if (safe != nullptr)
                safe->firstLaunchWindow.reset();
        });
    };
    firstLaunchWindow = std::make_unique<PanelWindow>(
        "Welcome to " + services::buildInfo().productName,
        std::make_unique<FirstLaunchPanel>(pages, deviceManager, themes, finish), this,
        [finish] { finish(std::nullopt); });
}

void MainComponent::showUpdatePanel()
{
    if (updateWindow != nullptr)
    {
        updateWindow->toFront(true);
        return;
    }
    showPanel(updateWindow, "Updates", [this] {
        return std::make_unique<UpdatePanel>(appServices, [this] {
            juce::MessageManager::callAsync([safe = juce::Component::SafePointer<MainComponent>(this)] {
                if (safe != nullptr)
                    safe->updateWindow.reset();
            });
        });
    });
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
    // Edits refused (someone else's lock, a recording) say why, unless a view did.
    if (const auto problem = session.takeEditProblem(); problem.isNotEmpty())
        statusBar.showWarning(problem);
    refreshCoop();
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

    // JUCE tells the components of windows on screen about the new look; a view in no
    // window (the UI tests') is told here, so that it lays itself out for it too.
    if (getPeer() == nullptr)
        sendLookAndFeelChange();
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
    // The copy of a co-op session goes back into its session.
    if (coop != nullptr && !coop->isActive() && controller::readMembership(file.getParentDirectory()))
        if (rejoinCoop(file.getParentDirectory()).wasOk())
            return;

    effectEditors.closeAll();
    const auto result = session.openProject(file, withPlugins);
    if (result.failed() && result.getErrorMessage().contains(model::newerFormatMessage) &&
        appServices.updates != nullptr)
    {
        // A project from a newer version: the way out is updating.
        juce::Logger::writeToLog("Alert: Could not open the project. " + result.getErrorMessage());
        const auto options = juce::MessageBoxOptions()
                                 .withIconType(juce::MessageBoxIconType::WarningIcon)
                                 .withTitle("Could not open the project")
                                 .withMessage(result.getErrorMessage())
                                 .withButton("Check for Updates")
                                 .withButton("OK")
                                 .withAssociatedComponent(this);
        showAlert(options, [safe = juce::Component::SafePointer<MainComponent>(this)](int button) {
            if (safe != nullptr && button == 0)
                safe->perform(InvocationInfo(checkForUpdates));
        });
        return;
    }
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
        withPlugins ? "Open a project" : "Open a project without its plugins", juce::File(), model::projectFilePatterns);
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
    // Save As proposes the current extension, also for a project saved under the old one.
    const auto current = session.getProjectFile();
    fileChooser = std::make_unique<juce::FileChooser>(
        title, current != juce::File() ? current.withFileExtension(model::projectFileExtension) : current,
        juce::String("*") + model::projectFileExtension);
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
                       openLogFolder, showAbout, checkForUpdates, toggleCheckAtLaunch,
                       toggleIncludePreReleases, toggleCrashReports, showPrivacy, coopHost, coopJoin,
                       coopShowSession, coopCopyInvite, coopPlayForEveryone, coopStopForEveryone,
                       coopToggleFollow, coopLeave, coopClose});

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
    case showAbout:
        result.setInfo("About " + services::buildInfo().productName,
                       "The version, the licences and where the app comes from", "Help", 0);
        break;
    case checkForUpdates:
        result.setInfo("Check for Updates...", "Looks for a newer version of the app", "Help", 0);
        result.setActive(appServices.updates != nullptr);
        break;
    case toggleCheckAtLaunch:
        result.setInfo("Check for Updates at Launch", "Looks for a newer version every time the app starts",
                       "Help", 0);
        result.setActive(appServices.settings != nullptr);
        result.setTicked(appServices.settings != nullptr &&
                         appServices.settings->getUpdatePreferences().checkAtLaunch);
        break;
    case toggleIncludePreReleases:
        result.setInfo("Include Pre-releases", "Offers release candidates too, to try them before everyone",
                       "Help", 0);
        result.setActive(appServices.settings != nullptr);
        result.setTicked(appServices.settings != nullptr && appServices.settings->getUpdatePreferences().channel ==
                                                                services::UpdateChannel::preRelease);
        break;
    case toggleCrashReports:
        result.setInfo("Send Crash Reports", "Sends a report when the app crashes, without personal data "
                       "(see Privacy)", "Help", 0);
        result.setActive(appServices.settings != nullptr && appServices.crashReports != nullptr &&
                         appServices.crashReports->canSend());
        result.setTicked(appServices.settings != nullptr &&
                         appServices.settings->getCrashReportConsent() == services::CrashReportConsent::allowed);
        break;
    case showPrivacy:
        result.setInfo("Privacy...", "What the app sends and what it keeps on this computer", "Help", 0);
        break;
    case coopHost:
        result.setInfo("Host a Session...", "Opens a copy of this project in a co-op session, to work on "
                       "it with others", "Co-op", 0);
        result.setActive(coop != nullptr && !coop->isActive() && !recording);
        break;
    case coopJoin:
        result.setInfo("Join a Session...", "Joins someone's co-op session with their invite", "Co-op", 0);
        result.setActive(coop != nullptr && !coop->isActive() && !recording);
        break;
    case coopShowSession:
        result.setInfo("Session...", "The co-op session: its invite and participants", "Co-op", 0);
        result.setActive(coop != nullptr && coop->findClient() != nullptr);
        break;
    case coopCopyInvite:
        result.setInfo("Copy Invite Link", "Copies the session's invite, to paste in a message", "Co-op", 0);
        result.setActive(coop != nullptr && coop->isActive() && coop->getInviteLink().isNotEmpty());
        break;
    case coopPlayForEveryone:
        result.setInfo("Play for Everyone", "Starts everyone who follows from the playhead, at the same moment",
                       "Co-op", 0);
        result.setActive(coop != nullptr && coop->isActive() && !recording);
        break;
    case coopStopForEveryone:
        result.setInfo("Stop for Everyone", "Stops everyone who follows", "Co-op", 0);
        result.setActive(coop != nullptr && coop->isActive());
        break;
    case coopToggleFollow:
        result.setInfo("Follow the Shared Transport", "Plays and stops when someone plays or stops for "
                       "everyone", "Co-op", 0);
        result.setActive(coop != nullptr);
        result.setTicked(coop != nullptr && coop->isFollowing());
        break;
    case coopLeave:
        result.setInfo("Leave Session...", "Leaves the co-op session; the copy stays, as an ordinary project",
                       "Co-op", 0);
        result.setActive(coop != nullptr && coop->isActive() && !recording);
        break;
    case coopClose:
        result.setInfo("Close Session...", "Ends the co-op session for everyone (the host only)", "Co-op", 0);
        result.setActive(coop != nullptr && coop->isActive() && coop->getClient().isHost() && !recording);
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
    case showAbout:
        showPanel(aboutWindow, "About " + services::buildInfo().productName, [this] {
            return std::make_unique<AboutPanel>(appServices.signature, appServices.licencesFolder);
        });
        break;
    case checkForUpdates:
        if (appServices.updates != nullptr)
        {
            appServices.updates->check(appServices.settings != nullptr ? appServices.settings->getUpdatePreferences()
                                                                       : services::UpdatePreferences{},
                                       true);
            showUpdatePanel();
        }
        break;
    case toggleCheckAtLaunch:
    case toggleIncludePreReleases:
        if (appServices.settings != nullptr)
        {
            auto preferences = appServices.settings->getUpdatePreferences();
            if (info.commandID == toggleCheckAtLaunch)
                preferences.checkAtLaunch = !preferences.checkAtLaunch;
            else
                preferences.channel = preferences.channel == services::UpdateChannel::stable
                                          ? services::UpdateChannel::preRelease
                                          : services::UpdateChannel::stable;
            appServices.settings->setUpdatePreferences(preferences);
            appServices.settings->save();
            commandManager.commandStatusChanged();
        }
        break;
    case toggleCrashReports:
        if (appServices.settings != nullptr && appServices.crashReports != nullptr)
        {
            const bool allow = appServices.settings->getCrashReportConsent() != services::CrashReportConsent::allowed;
            appServices.settings->setCrashReportConsent(allow ? services::CrashReportConsent::allowed
                                                              : services::CrashReportConsent::refused);
            appServices.settings->save();
            appServices.crashReports->setSendAllowed(allow);
            juce::Logger::writeToLog(allow ? "Crash reports: the user allowed sending them"
                                           : "Crash reports: the user stopped sending them");
            commandManager.commandStatusChanged();
        }
        break;
    case showPrivacy:
        juce::URL(services::buildInfo().repositoryUrl + "/blob/main/docs/PRIVACY.md").launchInDefaultBrowser();
        break;
    case coopHost:
        showCoopStart(CoopStartPanel::Mode::host);
        break;
    case coopJoin:
        showCoopStart(CoopStartPanel::Mode::join);
        break;
    case coopShowSession:
        showCoopSession();
        break;
    case coopCopyInvite:
        if (coop != nullptr)
            juce::SystemClipboard::copyTextToClipboard(coop->getInviteLink());
        break;
    case coopPlayForEveryone:
        if (coop != nullptr)
            coop->playForEveryone(session.getPlayhead());
        break;
    case coopStopForEveryone:
        if (coop != nullptr)
            coop->stopForEveryone();
        break;
    case coopToggleFollow:
        if (coop != nullptr)
        {
            coop->setFollowing(!coop->isFollowing());
            if (appServices.settings != nullptr)
            {
                auto preferences = appServices.settings->getCoopPreferences();
                preferences.follow = coop->isFollowing();
                appServices.settings->setCoopPreferences(preferences);
            }
            if (auto* panel = findCoopSessionPanel())
                panel->refresh();
        }
        break;
    case coopLeave:
        confirmLeaveCoop();
        break;
    case coopClose:
        confirmCloseCoop();
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
    return files.size() == 1 && model::isProjectFile(juce::File(files[0]));
}

void MainComponent::filesDropped(const juce::StringArray& files, int, int)
{
    openFile(juce::File(files[0]));
}

} // namespace daw::ui::desktop
