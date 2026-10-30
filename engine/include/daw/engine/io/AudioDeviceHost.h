#pragma once

#include "daw/engine/Engine.h"

#include <juce_audio_devices/juce_audio_devices.h>

namespace daw::engine::io
{

/**
    Connects an Engine to the computer's audio output through JUCE's
    AudioDeviceManager: opens the output device, prepares the engine whenever the
    device (re)starts, and runs Engine::process() from the device callback.

    Construct, initialise and destroy on the message thread. The device callback
    itself runs on the audio thread and is real-time safe; it is public only so that
    tests can drive it without a sound card.

    Note: AudioDeviceManager's own dispatch to its callbacks takes a lock that is
    only contended while callbacks are added or removed (here: at start-up and
    shut-down), as in every JUCE application.
*/
class AudioDeviceHost final : public juce::AudioIODeviceCallback
{
public:
    explicit AudioDeviceHost(Engine& engineToPlay);
    ~AudioDeviceHost() override;

    AudioDeviceHost(const AudioDeviceHost&) = delete;
    AudioDeviceHost& operator=(const AudioDeviceHost&) = delete;

    /** Opens a stereo output (no inputs): the device described by savedState (from
        getDeviceManager().createStateXml()) if it is available, otherwise the default
        one. Returns an error message, or an empty string on success. */
    juce::String initialise(const juce::XmlElement* savedState);

    /** For device selection in the UI, and for saving the chosen device. */
    [[nodiscard]] juce::AudioDeviceManager& getDeviceManager() noexcept { return deviceManager; }

    // juce::AudioIODeviceCallback
    void audioDeviceIOCallbackWithContext(const float* const* inputChannelData,
                                          int numInputChannels, float* const* outputChannelData,
                                          int numOutputChannels, int numSamples,
                                          const juce::AudioIODeviceCallbackContext& context) noexcept override;
    void audioDeviceAboutToStart(juce::AudioIODevice* device) override;
    void audioDeviceStopped() override;

private:
    // The engine renders at most stereo; further device outputs are cleared.
    static constexpr int engineChannels = 2;

    Engine& engine;
    juce::AudioDeviceManager deviceManager;
    juce::AudioBuffer<float> outputView; // refers to the device's buffers, owns nothing
    bool callbackAdded = false;
};

} // namespace daw::engine::io
