#pragma once

#include "daw/controller/Session.h"
#include "daw/ui/desktop/ArrangeView.h"
#include "daw/ui/desktop/LevelMeter.h"
#include "daw/ui/desktop/MixerView.h"
#include "daw/ui/desktop/TransportBar.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw::ui::desktop
{

/**
    Content of the main window: transport bar, arrange view, master meter and the
    mixer (shown or hidden with X).

    Edits go to the Session; the commands (menus and shortcuts) are handled here.
    It refreshes from the session and the engine on a timer.
*/
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
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
        toggleMixer
    };

    MainComponent(controller::Session& sessionToShow, juce::AudioDeviceManager& audioDeviceManager,
                  juce::ApplicationCommandManager& applicationCommandManager);
    ~MainComponent() override;

    /** Opens a project or imports an audio file (e.g. given on the command line). */
    void openFile(const juce::File& file);

    void showError(const juce::String& title, const juce::String& message);

    /** If the project has unsaved changes, asks whether to save them first; then
        runs next, unless the user cancelled (or saving failed). */
    void askToSaveChanges(std::function<void()> next);

    void resized() override;
    void paint(juce::Graphics& g) override;

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
    void togglePlayPause();
    void openProjectFile(const juce::File& file);
    void chooseAudioToImport();
    void chooseProjectToOpen();
    void chooseProjectToSave(std::function<void()> afterSaving = {});
    void saveThen(std::function<void()> afterSaving);
    void showAudioSettings();

    controller::Session& session;
    juce::AudioDeviceManager& deviceManager;
    juce::ApplicationCommandManager& commandManager;

    TransportBar transportBar;
    ArrangeView arrangeView;
    LevelMeter masterMeter;
    MixerView mixerView;
    bool mixerVisible = true;
    juce::TooltipWindow tooltipWindow{this};

    std::unique_ptr<juce::FileChooser> fileChooser;
    double lastRefreshSeconds = 0.0;
    engine::TransportState lastState = engine::TransportState::stopped;
    juce::uint64 lastRevision = 0;
    bool lastSelection = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace daw::ui::desktop
