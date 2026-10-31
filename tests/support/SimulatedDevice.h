#pragma once

#include "daw/engine/Engine.h"
#include "daw/engine/io/AudioDeviceHost.h"

#include <juce_audio_devices/juce_audio_devices.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw::test
{

/** An AudioIODevice that only describes itself: what AudioDeviceHost asks when the
    device starts. It never calls back on its own; SimulatedDevice does that. */
class FakeAudioDevice final : public juce::AudioIODevice
{
public:
    FakeAudioDevice(double sampleRate, int bufferSize, juce::BigInteger activeInputs,
                    int inputLatency = 0, int outputLatency = 0);

    juce::StringArray getOutputChannelNames() override;
    juce::StringArray getInputChannelNames() override;
    juce::Array<double> getAvailableSampleRates() override;
    juce::Array<int> getAvailableBufferSizes() override;
    int getDefaultBufferSize() override;
    juce::String open(const juce::BigInteger&, const juce::BigInteger&, double, int) override;
    void close() override {}
    bool isOpen() override { return true; }
    void start(juce::AudioIODeviceCallback*) override {}
    void stop() override {}
    bool isPlaying() override { return true; }
    juce::String getLastError() override { return {}; }
    int getCurrentBufferSizeSamples() override;
    double getCurrentSampleRate() override;
    int getCurrentBitDepth() override { return 24; }
    juce::BigInteger getActiveOutputChannels() const override;
    juce::BigInteger getActiveInputChannels() const override;
    int getOutputLatencyInSamples() override;
    int getInputLatencyInSamples() override;

private:
    double rate;
    int blockSize;
    juce::BigInteger inputs;
    int inputLatencySamples;
    int outputLatencySamples;
};

/**
    A sound card for tests: runs an Engine through AudioDeviceHost's device callback,
    block by block, with input the test controls.

    - Block sizes cycle through the list given: real devices may deliver blocks
      smaller (and, rarely, larger) than they announced.
    - Inputs: numInputs device channels, any of which can be switched off (JUCE then
      leaves them out of the callback). Each carries inputSignal(channel, stream
      index) plus whatever a loopback cable brings back.
    - Loopback: what the engine plays on an output channel at stream index s arrives
      on an input channel at s + latency. The latency must be at least the largest
      block: a real device never returns output within the block that produced it.
    - The stream index counts samples from the device start. It can start anywhere,
      e.g. near 2^32, to catch 32-bit overflow.

    The callback runs inside heapOperationsDuring(); preparing its input does not.
*/
class SimulatedDevice
{
public:
    static constexpr int numOutputs = 2;

    SimulatedDevice(engine::Engine& engineToDrive, double sampleRate, int numInputs,
                    std::vector<int> blockSizes);
    ~SimulatedDevice();

    /** The signal on every input channel; silence when empty. */
    std::function<float(int channel, juce::int64 streamIndex)> inputSignal;

    /** Before start(). */
    void setInputEnabled(int channel, bool enabled);
    void setReportedLatencies(int inputLatency, int outputLatency);
    void setStreamStart(juce::int64 index);
    void connectLoopback(int outputChannel, int inputChannel, int latency);

    /** Keep everything played (getOutput) or fed in (getInput). */
    void setRecordsOutput(bool shouldRecord) { recordOutput = shouldRecord; }
    void setRecordsInput(bool shouldRecord) { recordInput = shouldRecord; }

    /** Starts the device the way JUCE does: audioDeviceAboutToStart() on the host,
        which prepares the engine. */
    void start();

    /** Runs numSamples through the callback. Returns the heap operations made inside
        it, which must be 0. */
    std::size_t run(juce::int64 numSamples);

    [[nodiscard]] juce::int64 getStreamPosition() const noexcept { return streamPosition; }
    [[nodiscard]] const std::vector<float>& getOutput(int channel) const;
    [[nodiscard]] const std::vector<float>& getInput(int channel) const;
    [[nodiscard]] engine::io::AudioDeviceHost& getHost() noexcept { return host; }
    [[nodiscard]] FakeAudioDevice& getDevice() noexcept { return *device; }

private:
    struct Loopback
    {
        int outputChannel = 0;
        int inputChannel = 0;
        int latency = 0;
    };

    [[nodiscard]] float loopbackSample(int inputChannel, juce::int64 streamIndex) const;

    engine::io::AudioDeviceHost host;
    std::unique_ptr<FakeAudioDevice> device;

    double sampleRate;
    int numInputs;
    std::vector<int> blockSizes;
    std::size_t nextBlock = 0;
    int maximumBlockSize = 0;
    std::vector<bool> enabledInputs;
    int inputLatency = 0;
    int outputLatency = 0;

    std::vector<Loopback> loopbacks;
    std::vector<std::vector<float>> outputRing; // per output channel, by stream index
    std::size_t ringSize = 1;

    juce::int64 streamStart = 0;
    juce::int64 streamPosition = 0;

    juce::AudioBuffer<float> inputBuffer;  // one row per enabled input, packed as JUCE does
    juce::AudioBuffer<float> outputBuffer; // the device's outputs

    bool recordOutput = false;
    bool recordInput = false;
    std::vector<std::vector<float>> recordedOutput;
    std::vector<std::vector<float>> recordedInput;
};

} // namespace daw::test
