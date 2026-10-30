#include "daw/engine/io/AudioDeviceHost.h"

#include "support/RealtimeCheck.h"

#include <catch2/catch_test_macros.hpp>

#include <array>
#include <memory>
#include <vector>

using daw::engine::AudioData;
using daw::engine::Engine;
using daw::engine::io::AudioDeviceHost;

namespace
{

constexpr int blockSize = 512;

void loadConstant(Engine& engine, float value)
{
    auto audio = std::make_unique<AudioData>();
    audio->sampleRate = 48000.0;
    audio->samples.setSize(2, 48000);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(audio->samples.getWritePointer(channel), value, 48000);
    engine.setAudio(std::move(audio));
}

// Device buffers, pre-filled with junk that the callback must overwrite.
struct FakeDeviceOutputs
{
    explicit FakeDeviceOutputs(int numChannels)
        : storage(static_cast<std::size_t>(numChannels), std::vector<float>(blockSize, 9.0f))
    {
        for (auto& channel : storage)
            pointers.push_back(channel.data());
    }

    std::vector<std::vector<float>> storage;
    std::vector<float*> pointers;
};

} // namespace

TEST_CASE("The device callback feeds the engine to the first two outputs only", "[engine][device]")
{
    Engine engine;
    engine.prepare(48000.0, blockSize);
    loadConstant(engine, 0.5f);
    engine.play();

    AudioDeviceHost host(engine);
    FakeDeviceOutputs outputs(4);
    const juce::AudioIODeviceCallbackContext context{};

    const auto heapOperations = daw::test::heapOperationsDuring([&] {
        for (int i = 0; i < 10; ++i)
            host.audioDeviceIOCallbackWithContext(nullptr, 0, outputs.pointers.data(), 4, blockSize,
                                                  context);
    });

    CHECK(heapOperations == 0);
    CHECK(outputs.storage[0].back() == 0.5f);
    CHECK(outputs.storage[1].back() == 0.5f);
    CHECK(outputs.storage[2].back() == 0.0f);
    CHECK(outputs.storage[3].front() == 0.0f);
}

TEST_CASE("The device callback copes with mono and output-less devices", "[engine][device]")
{
    Engine engine;
    engine.prepare(48000.0, blockSize);
    loadConstant(engine, 0.25f);
    engine.play();

    AudioDeviceHost host(engine);
    const juce::AudioIODeviceCallbackContext context{};

    FakeDeviceOutputs mono(1);
    for (int i = 0; i < 4; ++i)
        host.audioDeviceIOCallbackWithContext(nullptr, 0, mono.pointers.data(), 1, blockSize, context);
    CHECK(mono.storage[0].back() == 0.25f);

    std::array<float*, 1> none{nullptr};
    host.audioDeviceIOCallbackWithContext(nullptr, 0, none.data(), 0, blockSize, context);
    CHECK(engine.getTransportState() == daw::engine::TransportState::playing);
}
