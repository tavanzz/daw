#pragma once

#include "daw/controller/Session.h"
#include "daw/engine/Engine.h"
#include "daw/engine/io/AudioDeviceHost.h"
#include "daw/ui/desktop/MainWindow.h"

#include <juce_data_structures/juce_data_structures.h>
#include <juce_events/juce_events.h>

#include <memory>

namespace daw::app
{

/**
    Owns the engine, the audio device, the session and the window, and keeps them in
    step: tells the session when the device's sample rate and latencies change,
    drives the session's update() from a timer, keeps the window title current, and
    remembers the audio device and its latency offset (per device pair and rate).

    Inputs: the device opens its inputs only with the microphone permission. The
    permission is asked for the first time a track needs an input (armed, or
    monitoring always), never at launch.

    Message thread only.
*/
class AppController final : private juce::ChangeListener, private juce::Timer
{
public:
    explicit AppController(const juce::String& applicationName);
    ~AppController() override;

    AppController(const AppController&) = delete;
    AppController& operator=(const AppController&) = delete;

    /** Opens a project, or imports an audio file (e.g. one given on the command line). */
    void openFile(const juce::File& file);

    /** Quits, after offering to save unsaved changes. */
    void requestQuit();

private:
    void updateWindowTitle();
    void saveDeviceState();
    void deviceChanged();
    void openInputsIfNeeded();
    [[nodiscard]] juce::String latencyOffsetKey();
    [[nodiscard]] ui::desktop::MainComponent& view();

    void changeListenerCallback(juce::ChangeBroadcaster* source) override;
    void timerCallback() override;

    const juce::String applicationName;
    juce::ApplicationProperties settings;

    // Destroyed in reverse order: the window refers to the session, the session to
    // the engine, and the device must stop before the engine goes.
    engine::Engine engine;
    engine::io::AudioDeviceHost deviceHost{engine};
    controller::Session session{engine};
    std::unique_ptr<ui::desktop::MainWindow> window;

    juce::String shownTitle;

    // Inputs: asking the user, or the device has none (until the device changes).
    bool askingForInputs = false;
    bool noInputsOnDevice = false;
    juce::String offsetKey; // the setting the current latency offset belongs to
    int savedOffset = 0;
    std::shared_ptr<bool> alive = std::make_shared<bool>(true);
};

} // namespace daw::app
