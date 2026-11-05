#pragma once

#include "daw/controller/PluginLibrary.h"
#include "daw/controller/Session.h"
#include "daw/ui/desktop/ArrangeView.h"
#include "daw/ui/desktop/EffectEditorWindow.h"
#include "daw/ui/desktop/HazardBand.h"
#include "daw/ui/desktop/LevelMeter.h"
#include "daw/ui/desktop/MixerView.h"
#include "daw/ui/desktop/PanelWindow.h"
#include "daw/ui/desktop/PianoRoll.h"
#include "daw/ui/desktop/PluginBrowserPanel.h"
#include "daw/ui/desktop/StatusBar.h"
#include "daw/ui/desktop/ThemeManager.h"
#include "daw/ui/desktop/TransportBar.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw::ui::desktop
{

/**
    Content of the main window: transport bar, arrange view, master meter, the mixer
    (shown or hidden with X) and the status bar.

    Edits go to the Session; the commands (menus and shortcuts) are handled here.
    It refreshes from the session and the engine on a timer.
*/
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::DragAndDropContainer,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer,
                            private ThemeManager::Listener
{
public:
    enum CommandIds
    {
        newProject = 0x2001,
        openProject,
        saveProject,
        saveProjectAs,
        importAudio,
        exportMix,
        audioSettings,
        undo,
        redo,
        newTrack,
        deleteSelection,
        playPause,
        stop,
        toggleLoop,
        toggleSnap,
        zoomIn,
        zoomOut,
        zoomToFit,
        toggleMixer,
        record,
        discardRecording,
        toggleMetronome,
        toggleCountIn,
        togglePunch,
        newBus,
        toggleAutomation,
        showThemePreviews,
        showAnimations,
        toggleAllAnimations,
        newMidiTrack,
        duplicate,
        togglePianoRoll,
        quantiseNotes,
        exportMidi,
        panic,
        togglePluginBrowser,
        openWithoutPlugins,
        showPluginManager,
        // One command per theme of the library, in menu order (Cmd+1, Cmd+2, ...).
        firstTheme = 0x2100,
        lastTheme = firstTheme + 8
    };

    /** pluginLibrary: the app's plugins (null: only the built-in effects). */
    MainComponent(controller::Session& sessionToShow, juce::AudioDeviceManager& audioDeviceManager,
                  juce::ApplicationCommandManager& applicationCommandManager,
                  ThemeManager& themeManager, controller::PluginLibrary* libraryToUse = nullptr);
    ~MainComponent() override;

    /** Opens a project or imports an audio file (e.g. given on the command line). */
    void openFile(const juce::File& file);

    /** Opens windows for a review of the interface (the app's --show option):
        "previews" (the themes), "animations" (their settings), "mixer", "editors" (those
        of the first track with effects). */
    void showForReview(const juce::StringArray& what);

    void showError(const juce::String& title, const juce::String& message);

    /** If the project has unsaved changes, asks whether to save them first; then
        runs next, unless the user cancelled (or saving failed). */
    void askToSaveChanges(std::function<void()> next);

    void resized() override;
    void paint(juce::Graphics& g) override;
    void lookAndFeelChanged() override;

    [[nodiscard]] ArrangeView& getArrangeViewForTesting() noexcept { return arrangeView; }
    [[nodiscard]] PianoRoll& getPianoRollForTesting() noexcept { return pianoRoll; }
    [[nodiscard]] EffectEditors& getEffectEditorsForTesting() noexcept { return effectEditors; }
    [[nodiscard]] PluginBrowserPanel& getPluginBrowserForTesting() noexcept { return pluginBrowser; }
    [[nodiscard]] MixerView& getMixerForTesting() noexcept { return mixerView; }

    /** Where plugin editor windows were last; the app keeps them in the user settings. */
    [[nodiscard]] WindowPositions& getPluginWindowPositions() noexcept { return pluginWindows; }

    /** Opens a MIDI clip in the piano roll (the lower panel), as a double-click does. */
    void editClip(model::ObjectId clipId);

    // juce::ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandId, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

    // juce::FileDragAndDropTarget: project files dropped anywhere (audio files go to
    // the track lanes).
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void themeAboutToChange() override;
    void animationSettingsChanged() override;
    void showMixer(bool shouldShow);
    void showPianoRoll(bool shouldShow);
    void showLowerPanel(bool shouldShow);
    void chooseMidiExport();
    void themeChanged() override;
    void showPanel(std::unique_ptr<PanelWindow>& window, const juce::String& title,
                   const std::function<std::unique_ptr<juce::Component>()>& makeContent);
    void mixerFrame(double seconds);
    void togglePlayPause();
    void openProjectFile(const juce::File& file, bool withPlugins = true);
    void showPluginBrowser(bool shouldShow);
    void reportUnavailablePlugins();
    void offerRecovery();
    void chooseAudioToImport();
    void chooseProjectToOpen(bool withPlugins = true);
    void chooseProjectToSave(std::function<void()> afterSaving = {},
                             const juce::String& title = "Save the project");
    void saveThen(std::function<void()> afterSaving);
    void showAudioSettings();
    void toggleRecording();
    void startRecording();
    void confirmDiscardRecording();

    controller::Session& session;
    juce::AudioDeviceManager& deviceManager;
    juce::ApplicationCommandManager& commandManager;
    ThemeManager& themes;
    controller::PluginLibrary* pluginLibrary;

    TransportBar transportBar;
    HazardBand hazardBand;
    ArrangeView arrangeView;
    LevelMeter masterMeter;
    MixerView mixerView;
    PianoRoll pianoRoll{session};
    bool mixerVisible = true;      // the lower panel: the mixer or the piano roll
    bool showingPianoRoll = false; // which of them
    float mixerAmount = 1.0f; // how much of the mixer shows, while it slides
    float mixerFrom = 1.0f;
    double mixerSlideStart = -1.0;
    AnimationLink mixerSlide{*this, appearance::AnimationEffect::panelTransitions,
                             [this](double seconds) { mixerFrame(seconds); }};
    std::unique_ptr<ThemeCrossfade> crossfade;
    std::unique_ptr<PanelWindow> previewsWindow;
    std::unique_ptr<PanelWindow> animationsWindow;
    std::unique_ptr<PanelWindow> pluginManagerWindow;
    StatusBar statusBar{session};
    PluginBrowserPanel pluginBrowser;
    bool browserVisible = false;
    int unavailableCheck = 0; // updates to wait before naming plugins a project lacks
    WindowPositions pluginWindows;
    EffectEditors effectEditors{session, pluginWindows};
    double lastSlowWarning = 0.0;
    juce::TooltipWindow tooltipWindow{this};

    std::unique_ptr<juce::FileChooser> fileChooser;
    double lastRefreshSeconds = 0.0;
    engine::TransportState lastState = engine::TransportState::stopped;
    juce::uint64 lastRevision = 0;
    bool lastSelection = false;
    bool lastRecording = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace daw::ui::desktop
