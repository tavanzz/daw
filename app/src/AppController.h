#pragma once

#include "daw/engine/Engine.h"
#include "daw/engine/dsp/WaveformPeaks.h"
#include "daw/engine/io/AudioDeviceHost.h"
#include "daw/model/Project.h"
#include "daw/ui/desktop/MainWindow.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <memory>

namespace daw::app
{

/**
    Owns the engine, the audio device, the project and the window, and keeps them
    in step: loads audio files on a background thread, mirrors loop and file choices
    into the project, saves and opens project files, and remembers the audio device.

    Message thread only.
*/
class AppController final : private juce::ChangeListener, private juce::Timer
{
public:
    explicit AppController(const juce::String& applicationName);
    ~AppController() override;

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    /** Opens an audio or project file, e.g. one given on the command line. */
    void openFile(const juce::File& file);

    void openAudioFile(const juce::File& file);
    void openProject(const juce::File& file);
    bool saveProject();
    void saveProjectAs(const juce::File& file);
    void setLooping(bool shouldLoop);

private:
    struct LoadOutcome;

    void finishLoading(int generation, const juce::File& file, LoadOutcome& outcome);
    void playWholeFile(engine::AudioData audio);
    void restoreDisplay();
    void updateWindowTitle();
    void saveDeviceState();
    [[nodiscard]] double loadingSampleRate() const;
    [[nodiscard]] ui::desktop::MainComponent& view();

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    const juce::String applicationName;
    juce::ApplicationProperties settings;

    engine::Engine engine;
    engine::io::AudioDeviceHost deviceHost{engine};

    model::Project project;
    juce::File projectFile; // empty until saved or opened

    // The audio the engine has (or is about to have), and how it is shown.
    juce::File audioFile;
    double audioSampleRate = 0.0;
    juce::String audioDetails;
    std::shared_ptr<const engine::dsp::WaveformPeaks> audioPeaks;

    // Destroyed before the members above: the window refers to the engine and the
    // device manager, and a finishing load posts back to this object.
    juce::ThreadPool loader{juce::ThreadPoolOptions{}.withThreadName("Audio file loader").withNumberOfThreads(1)};
    int loadGeneration = 0;
    std::unique_ptr<ui::desktop::MainWindow> window;

    JUCE_DECLARE_WEAK_REFERENCEABLE(AppController)
};

} // namespace daw::app
