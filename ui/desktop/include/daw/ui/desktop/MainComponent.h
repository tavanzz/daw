#pragma once

#include "daw/engine/Engine.h"
#include "daw/engine/dsp/WaveformPeaks.h"
#include "daw/ui/desktop/LevelMeter.h"
#include "daw/ui/desktop/TransportBar.h"
#include "daw/ui/desktop/WaveformView.h"

#include <juce_audio_devices/juce_audio_devices.h>
#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>

namespace daw::ui::desktop
{

/**
    Content of the main window: transport, file name, waveform and level meter.

    It reads the engine's published state on a timer and sends it transport
    commands directly. Anything involving files, the project or settings is handed
    to the application through the on... callbacks, which app/ sets.
*/
class MainComponent final : public juce::Component,
                            public juce::ApplicationCommandTarget,
                            public juce::FileDragAndDropTarget,
                            private juce::Timer
{
public:
    enum CommandIds
    {
        openAudioFile = 0x2001,
        openProject,
        saveProject,
        saveProjectAs,
        audioSettings,
        playPause,
        stop,
        toggleLoop
    };

    MainComponent(engine::Engine& engineToShow, juce::AudioDeviceManager& audioDeviceManager,
                  juce::ApplicationCommandManager& applicationCommandManager);
    ~MainComponent() override;

    // Requests to the application.
    std::function<void(const juce::File&)> onOpenAudioFile;
    std::function<void(const juce::File&)> onOpenProject;
    std::function<bool()> onSaveProject; ///< false if the project has no file yet
    std::function<void(const juce::File&)> onSaveProjectAs;
    std::function<void(bool)> onLoopToggled;

    // What the application wants shown.
    void showMessage(const juce::String& text);
    void showAudio(const juce::String& fileName, const juce::String& details,
                   std::shared_ptr<const engine::dsp::WaveformPeaks> peaks);
    void showError(const juce::String& title, const juce::String& message);

    /** The text shown when no file is loaded. */
    [[nodiscard]] static juce::String emptyMessage();

    void resized() override;
    void paint(juce::Graphics& g) override;

    // juce::ApplicationCommandTarget
    juce::ApplicationCommandTarget* getNextCommandTarget() override;
    void getAllCommands(juce::Array<juce::CommandID>& commands) override;
    void getCommandInfo(juce::CommandID commandId, juce::ApplicationCommandInfo& result) override;
    bool perform(const InvocationInfo& info) override;

    // juce::FileDragAndDropTarget
    bool isInterestedInFileDrag(const juce::StringArray& files) override;
    void filesDropped(const juce::StringArray& files, int x, int y) override;

private:
    void timerCallback() override;
    void togglePlayPause();
    void chooseAudioFile();
    void chooseProjectToOpen();
    void chooseProjectToSave();
    void showAudioSettings();

    engine::Engine& engine;
    juce::AudioDeviceManager& deviceManager;
    juce::ApplicationCommandManager& commandManager;

    TransportBar transportBar;
    juce::Label fileLabel;
    juce::Label detailsLabel;
    WaveformView waveform;
    LevelMeter meter;
    juce::TooltipWindow tooltipWindow{this};

    std::unique_ptr<juce::FileChooser> fileChooser;
    bool hasAudio = false;
    bool dragHover = false;
    double lastRefreshSeconds = 0.0;
    engine::TransportState lastState = engine::TransportState::stopped;
    bool lastLooping = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainComponent)
};

} // namespace daw::ui::desktop
