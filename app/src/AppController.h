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
    step: tells the session when the device's sample rate changes, drives the
    session's update() from a timer, keeps the window title current, and remembers
    the audio device.

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
};

} // namespace daw::app
