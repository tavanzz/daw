#include "support/SimulatedDevice.h"

#include "support/RealtimeCheck.h"

#include <algorithm>

namespace daw::test
{

FakeAudioDevice::FakeAudioDevice(double sampleRate, int bufferSize, juce::BigInteger activeInputs,
                                 int inputLatency, int outputLatency)
    : juce::AudioIODevice("Simulated", "Simulated")
    , rate(sampleRate)
    , blockSize(bufferSize)
    , inputs(std::move(activeInputs))
    , inputLatencySamples(inputLatency)
    , outputLatencySamples(outputLatency)
{
}

juce::StringArray FakeAudioDevice::getOutputChannelNames()
{
    return {"Out 1", "Out 2"};
}

juce::StringArray FakeAudioDevice::getInputChannelNames()
{
    juce::StringArray names;
    for (int channel = 0; channel <= inputs.getHighestBit(); ++channel)
        names.add("In " + juce::String(channel + 1));
    return names;
}

juce::Array<double> FakeAudioDevice::getAvailableSampleRates()
{
    return {rate};
}

juce::Array<int> FakeAudioDevice::getAvailableBufferSizes()
{
    return {blockSize};
}

int FakeAudioDevice::getDefaultBufferSize()
{
    return blockSize;
}

juce::String FakeAudioDevice::open(const juce::BigInteger&, const juce::BigInteger&, double, int)
{
    return {};
}

int FakeAudioDevice::getCurrentBufferSizeSamples()
{
    return blockSize;
}

double FakeAudioDevice::getCurrentSampleRate()
{
    return rate;
}

juce::BigInteger FakeAudioDevice::getActiveOutputChannels() const
{
    juce::BigInteger outputs;
    outputs.setRange(0, SimulatedDevice::numOutputs, true);
    return outputs;
}

juce::BigInteger FakeAudioDevice::getActiveInputChannels() const
{
    return inputs;
}

int FakeAudioDevice::getOutputLatencyInSamples()
{
    return outputLatencySamples;
}

int FakeAudioDevice::getInputLatencyInSamples()
{
    return inputLatencySamples;
}

SimulatedDevice::SimulatedDevice(engine::Engine& engineToDrive, double rate, int inputs,
                                 std::vector<int> sizes)
    : host(engineToDrive)
    , sampleRate(rate)
    , numInputs(inputs)
    , blockSizes(std::move(sizes))
    , enabledInputs(static_cast<std::size_t>(inputs), true)
    , outputRing(numOutputs)
    , recordedOutput(numOutputs)
    , recordedInput(static_cast<std::size_t>(inputs))
{
    jassert(!blockSizes.empty());
    maximumBlockSize = *std::max_element(blockSizes.begin(), blockSizes.end());
}

SimulatedDevice::~SimulatedDevice() = default;

void SimulatedDevice::setInputEnabled(int channel, bool enabled)
{
    enabledInputs[static_cast<std::size_t>(channel)] = enabled;
}

void SimulatedDevice::setReportedLatencies(int newInputLatency, int newOutputLatency)
{
    inputLatency = newInputLatency;
    outputLatency = newOutputLatency;
}

void SimulatedDevice::setStreamStart(juce::int64 index)
{
    streamStart = index;
    streamPosition = index;
}

void SimulatedDevice::connectLoopback(int outputChannel, int inputChannel, int latency)
{
    jassert(latency >= maximumBlockSize);
    loopbacks.push_back({outputChannel, inputChannel, latency});
    ringSize = std::max(ringSize, static_cast<std::size_t>(latency + maximumBlockSize));
}

void SimulatedDevice::start()
{
    juce::BigInteger active;
    for (int channel = 0; channel < numInputs; ++channel)
        active.setBit(channel, enabledInputs[static_cast<std::size_t>(channel)]);

    device = std::make_unique<FakeAudioDevice>(sampleRate, maximumBlockSize, active, inputLatency,
                                               outputLatency);

    inputBuffer.setSize(std::max(1, active.countNumberOfSetBits()), maximumBlockSize);
    outputBuffer.setSize(numOutputs, maximumBlockSize);
    for (auto& ring : outputRing)
        ring.assign(ringSize, 0.0f);

    host.audioDeviceAboutToStart(device.get());
}

float SimulatedDevice::loopbackSample(int inputChannel, juce::int64 streamIndex) const
{
    float sum = 0.0f;
    for (const auto& loopback : loopbacks)
    {
        const juce::int64 played = streamIndex - loopback.latency;
        if (loopback.inputChannel == inputChannel && played >= streamStart)
            sum += outputRing[static_cast<std::size_t>(loopback.outputChannel)]
                             [static_cast<std::size_t>(played) % ringSize];
    }
    return sum;
}

std::size_t SimulatedDevice::run(juce::int64 numSamples)
{
    jassert(device != nullptr);
    std::size_t heapOperations = 0;
    const juce::AudioIODeviceCallbackContext context{};

    for (juce::int64 done = 0; done < numSamples;)
    {
        const int blockSize = static_cast<int>(
            std::min<juce::int64>(blockSizes[nextBlock], numSamples - done));
        nextBlock = (nextBlock + 1) % blockSizes.size();

        // The device's input, packed: only the channels that are switched on.
        std::vector<const float*> inputs;
        for (int channel = 0, row = 0; channel < numInputs; ++channel)
        {
            if (!enabledInputs[static_cast<std::size_t>(channel)])
                continue;

            float* samples = inputBuffer.getWritePointer(row++);
            for (int i = 0; i < blockSize; ++i)
            {
                const juce::int64 index = streamPosition + i;
                samples[i] = (inputSignal ? inputSignal(channel, index) : 0.0f) +
                             loopbackSample(channel, index);
            }

            inputs.push_back(samples);
            if (recordInput)
                recordedInput[static_cast<std::size_t>(channel)].insert(
                    recordedInput[static_cast<std::size_t>(channel)].end(), samples,
                    samples + blockSize);
        }

        float* const outputs[] = {outputBuffer.getWritePointer(0), outputBuffer.getWritePointer(1)};
        const auto numDelivered = static_cast<int>(inputs.size());
        heapOperations += heapOperationsDuring([&] {
            host.audioDeviceIOCallbackWithContext(inputs.data(), numDelivered, outputs, numOutputs,
                                                  blockSize, context);
        });

        for (int channel = 0; channel < numOutputs; ++channel)
        {
            const float* played = outputBuffer.getReadPointer(channel);
            auto& ring = outputRing[static_cast<std::size_t>(channel)];
            for (int i = 0; i < blockSize; ++i)
                ring[static_cast<std::size_t>(streamPosition + i) % ringSize] = played[i];

            if (recordOutput)
                recordedOutput[static_cast<std::size_t>(channel)].insert(
                    recordedOutput[static_cast<std::size_t>(channel)].end(), played,
                    played + blockSize);
        }

        streamPosition += blockSize;
        done += blockSize;
    }

    return heapOperations;
}

const std::vector<float>& SimulatedDevice::getOutput(int channel) const
{
    return recordedOutput[static_cast<std::size_t>(channel)];
}

const std::vector<float>& SimulatedDevice::getInput(int channel) const
{
    return recordedInput[static_cast<std::size_t>(channel)];
}

} // namespace daw::test
