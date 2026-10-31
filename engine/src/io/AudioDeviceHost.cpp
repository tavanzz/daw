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

juce::String AudioDeviceHost::initialise(const juce::XmlElement* savedState, bool withInputs)
{
    const auto error = deviceManager.initialise(withInputs ? AudioInputs::maxChannels : 0,
                                                engineChannels, savedState, true);

    if (!callbackAdded)
    {
        deviceManager.addAudioCallback(this);
        callbackAdded = true;
    }

    return error;
}

void AudioDeviceHost::audioDeviceIOCallbackWithContext(
    const float* const* inputChannelData, int numDeliveredInputs,
    float* const* outputChannelData, int numOutputChannels, int numSamples,
    const juce::AudioIODeviceCallbackContext& /*context*/) noexcept
{
    inputPointers.fill(nullptr);
    const int delivered =
        inputChannelData != nullptr ? std::min(numDeliveredInputs, numInputChannels) : 0;
    for (int i = 0; i < delivered; ++i)
        inputPointers[static_cast<std::size_t>(inputChannelNumbers[static_cast<std::size_t>(i)])] =
            inputChannelData[i];

    const AudioInputs inputs{inputPointers.data(), delivered > 0 ? numDeviceInputChannels : 0};

    // Referring to at most two channels stays within AudioBuffer's built-in
    // channel-pointer space, so this never allocates.
    const int engineOutputs = std::min(numOutputChannels, engineChannels);
    outputView.setDataToReferTo(outputChannelData, engineOutputs, numSamples);
    engine.process(inputs, outputView);

    for (int channel = engineOutputs; channel < numOutputChannels; ++channel)
        if (outputChannelData[channel] != nullptr)
            juce::FloatVectorOperations::clear(outputChannelData[channel], numSamples);
}

void AudioDeviceHost::audioDeviceAboutToStart(juce::AudioIODevice* device)
{
    const auto active = device->getActiveInputChannels();
    numInputChannels = 0;
    numDeviceInputChannels = 0;

    for (int channel = active.findNextSetBit(0);
         channel >= 0 && channel < AudioInputs::maxChannels;
         channel = active.findNextSetBit(channel + 1))
    {
        inputChannelNumbers[static_cast<std::size_t>(numInputChannels++)] = channel;
        numDeviceInputChannels = channel + 1;
    }

    engine.prepare(device->getCurrentSampleRate(), device->getCurrentBufferSizeSamples());
}

void AudioDeviceHost::audioDeviceStopped()
{
}

} // namespace daw::engine::io
