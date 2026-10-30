#include "daw/engine/io/AudioDeviceHost.h"

#include <algorithm>

namespace daw::engine::io
{

AudioDeviceHost::AudioDeviceHost(Engine& engineToPlay)
    : engine(engineToPlay)
{
}

AudioDeviceHost::~AudioDeviceHost()
{
    if (callbackAdded)
        deviceManager.removeAudioCallback(this);

    deviceManager.closeAudioDevice();
}

juce::String AudioDeviceHost::initialise(const juce::XmlElement* savedState)
{
    const auto error = deviceManager.initialise(0, engineChannels, savedState, true);

    if (!callbackAdded)
    {
        deviceManager.addAudioCallback(this);
        callbackAdded = true;
    }

    return error;
}

void AudioDeviceHost::audioDeviceIOCallbackWithContext(
    const float* const* /*inputChannelData*/, int /*numInputChannels*/,
    float* const* outputChannelData, int numOutputChannels, int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/) noexcept
{
    // Referring to at most two channels stays within AudioBuffer's built-in
    // channel-pointer space, so this never allocates.
    const int engineOutputs = std::min(numOutputChannels, engineChannels);
    outputView.setDataToReferTo(outputChannelData, engineOutputs, numSamples);
    engine.process(outputView);

    for (int channel = engineOutputs; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
}

void AudioDeviceHost::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    engine.prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioDeviceHost::audioDeviceStopped()
{
}

} // namespace daw::engine::io
