#pragma once

#include "daw/engine/Engine.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <array>
#include <atomic>

namespace daw::engine::io
{

/**
    Connects an Engine to the computer's audio interface through JUCE's
    AudioDeviceManager: opens the devices, prepares the engine whenever they
    (re)start, and runs Engine::process() from the device callback.

    The engine sees the inputs by device channel number: JUCE delivers only the
    channels that are switched on, packed together, and the callback puts each one
    back at its number (AudioInputs), so a track's input stays the same channel
    whichever others are switched on or off.

    Construct, initialise and destroy on the message thread. The device callback
    itself runs on the audio thread and is real-time safe; it is public only so that
    tests can drive it without a sound card.

    Note: AudioDeviceManager's own dispatch to its callbacks takes a lock that is
    only contended while callbacks are added or removed (here: at start-up and
    shut-down), as in every JUCE application.
*/
class AudioDeviceHost final : public juce::AudioIODeviceCallback,
                              private juce::MidiInputCallback
{
public:
    /** The clock audio blocks and MIDI are stamped with (midi::hostSeconds()). */
    using Clock = double (*)() noexcept;
    explicit AudioDeviceHost(Engine& engineToPlay);
    ~AudioDeviceHost() override;

    AudioDeviceHost(const AudioDeviceHost&) = delete;
    AudioDeviceHost& operator=(const AudioDeviceHost&) = delete;

    /** Opens a stereo output, and with withInputs also the inputs (all channels of
        the input device, up to AudioInputs::maxChannels): the devices described by
        savedState (from getDeviceManager().createStateXml()) if they are available,
        otherwise the default ones. Without withInputs no input device is opened,
        even one savedState names (on macOS that would ask for the microphone
        permission). Returns an error message, or an empty string on success. */
    juce::String initialise(const juce::XmlElement* savedState, bool withInputs = false);

    /** Opens the inputs too, if none is open: the default input device, all its
        channels. Returns an error message, or an empty string on success. */
    juce::String openInputs();

    /** Whether the device delivers any input channel. */
    [[nodiscard]] bool hasInputs() const;

    /** MIDI input thread: hands a device's message to the engine, stamped with its
        time stamp (seconds on the host clock, as JUCE stamps incoming MIDI).
        Public so that tests can play a keyboard without one. */
    void receiveMidi(int deviceIndex, const juce::MidiMessage& message) noexcept;

    /** The index the engine knows a MIDI input device by, given once per identifier
        and kept while the app runs (the last index for any beyond
        midi::maximumInputDevices). Message thread. */
    int getMidiDeviceIndex(const juce::String& identifier);

    /** For tests: a clock of their own. Before the device starts. */
    void setClock(Clock newClock) noexcept { clock = newClock; }

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
    void handleIncomingMidiMessage(juce::MidiInput* source,
                                   const juce::MidiMessage& message) override;

    // The engine renders at most stereo; further device outputs are cleared.
    static constexpr int engineChannels = 2;

    Engine& engine;
    juce::AudioDeviceManager deviceManager;
    juce::AudioBuffer<float> outputView; // refers to the device's buffers, owns nothing
    bool callbackAdded = false;

    // The device channel number of each input JUCE delivers, in delivery order; set
    // while the device is stopped (audioDeviceAboutToStart), read by the callback.
    std::array<int, AudioInputs::maxChannels> inputChannelNumbers{};
    int numInputChannels = 0;
    int numDeviceInputChannels = 0; // highest channel number switched on + 1
    std::array<const float*, AudioInputs::maxChannels> inputPointers{};

    Clock clock = &midi::hostSeconds;

    // The identifiers' hashes by device index: written on the message thread, read
    // by the MIDI threads.
    std::array<std::atomic<juce::int64>, midi::maximumInputDevices> midiDeviceHashes{};
    std::atomic<int> numMidiDevices{0};
};

} // namespace daw::engine::io
