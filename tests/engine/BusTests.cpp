#include "daw/engine/Engine.h"
#include "daw/engine/Mixer.h"

#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <memory>
#include <vector>

using Catch::Matchers::WithinAbs;
using daw::engine::Arrangement;
using daw::engine::ArrangementBus;
using daw::engine::ArrangementSend;
using daw::engine::ArrangementTrack;
using daw::engine::ChainSlot;
using daw::engine::Engine;
using daw::engine::Mixer;
using daw::engine::MixState;
using daw::engine::fx::EffectInstance;
using daw::engine::fx::ProcessSetup;
using daw::test::GainEffect;
using daw::test::LatencyEffect;
using daw::test::makeConstantSource;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int rampLength = 960; // 20 ms

ChainSlot effect(std::unique_ptr<daw::engine::fx::Effect> effectToHost)
{
    ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = 512;
    ChainSlot slot;
    slot.instance = std::make_shared<EffectInstance>(std::move(effectToHost), setup);
    return slot;
}

// Tracks each playing a constant stereo file from 0; ids 1, 2, ...
std::unique_ptr<Arrangement> tracksOf(std::initializer_list<float> values, int length = 96000)
{
    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = sampleRate;
    arrangement->length = length;
    juce::int64 id = 1;
    for (const float value : values)
    {
        const auto source = makeConstantSource(2, length, value, sampleRate);
        ArrangementTrack track;
        track.id = id++;
        track.segments.push_back({source.get(), 0, length, 0, 0, 0});
        arrangement->tracks.push_back(std::move(track));
        arrangement->sources.push_back(source);
    }
    return arrangement;
}

ArrangementBus& addBus(Arrangement& arrangement, juce::int64 id, float left = 1.0f,
                       float right = 1.0f)
{
    ArrangementBus bus;
    bus.id = id;
    bus.leftGain = left;
    bus.rightGain = right;
    arrangement.buses.push_back(std::move(bus));
    return arrangement.buses.back();
}

void addSend(Arrangement& arrangement, std::size_t track, juce::int64 sendId, juce::int64 bus,
             float gain, bool preFader)
{
    ArrangementSend send;
    send.id = sendId;
    send.bus = bus;
    for (std::size_t i = 0; i < arrangement.buses.size(); ++i)
        if (arrangement.buses[i].id == bus)
            send.busIndex = static_cast<int>(i);
    send.gain = gain;
    send.preFader = preFader;
    arrangement.tracks[track].sends.push_back(send);
}

struct Output
{
    std::vector<float> left;
    std::vector<float> right;
    std::size_t heapOperations = 0;
};

Output render(Mixer& mixer, const Arrangement& arrangement, MixState& state,
              juce::int64 start, int length, int blockSize)
{
    Output output;
    juce::AudioBuffer<float> block(2, blockSize);
    for (int done = 0; done < length; done += blockSize)
    {
        const int piece = std::min(blockSize, length - done);
        output.heapOperations += daw::test::heapOperationsDuring(
            [&] { mixer.render(arrangement, state, start + done, block, 0, piece); });
        output.left.insert(output.left.end(), block.getReadPointer(0), block.getReadPointer(0) + piece);
        output.right.insert(output.right.end(), block.getReadPointer(1),
                            block.getReadPointer(1) + piece);
    }
    return output;
}

bool all(const std::vector<float>& samples, float value, std::size_t from = 0)
{
    return std::all_of(samples.begin() + static_cast<std::ptrdiff_t>(from), samples.end(),
                       [value](float sample) { return sample == value; });
}

} // namespace

TEST_CASE("A post-fader send takes the track after its volume and pan", "[engine][bus]")
{
    auto arrangement = tracksOf({1.0f});
    arrangement->tracks[0].leftGain = 0.5f;
    arrangement->tracks[0].rightGain = 0.25f;
    addBus(*arrangement, 10);
    addSend(*arrangement, 0, 20, 10, 0.5f, false);

    Mixer mixer;
    mixer.prepare(sampleRate, 512);
    auto state = MixState::startingAt(*arrangement);
    const auto output = render(mixer, *arrangement, state, 0, 2048, 512);

    // Track (0.5, 0.25) plus the bus, half of it (0.25, 0.125).
    CHECK(all(output.left, 0.75f));
    CHECK(all(output.right, 0.375f));
    CHECK(output.heapOperations == 0);
}

TEST_CASE("A pre-fader send takes the track after its inserts, before volume and pan",
          "[engine][bus]")
{
    auto arrangement = tracksOf({1.0f});
    arrangement->tracks[0].leftGain = 0.25f;
    arrangement->tracks[0].rightGain = 0.125f;
    arrangement->tracks[0].inserts.slots.push_back(effect(std::make_unique<GainEffect>(0.5f)));
    addBus(*arrangement, 10);
    addSend(*arrangement, 0, 20, 10, 1.0f, true);

    Mixer mixer;
    mixer.prepare(sampleRate, 512);
    auto state = MixState::startingAt(*arrangement);
    auto output = render(mixer, *arrangement, state, 0, 1024, 512);
    CHECK(all(output.left, 0.125f + 0.5f));
    CHECK(all(output.right, 0.0625f + 0.5f));

    // With the fader all the way down the bus still gets it.
    arrangement->tracks[0].leftGain = 0.0f;
    arrangement->tracks[0].rightGain = 0.0f;
    state = MixState::startingAt(*arrangement);
    output = render(mixer, *arrangement, state, 1024, 1024, 512);
    CHECK(all(output.left, 0.5f));
    CHECK(all(output.right, 0.5f));
    CHECK(output.heapOperations == 0);
}

TEST_CASE("A bus runs the sum of its sends through its effects and fader into the master",
          "[engine][bus]")
{
    auto arrangement = tracksOf({0.25f, 0.125f, 0.0625f});
    auto busEffect = std::make_unique<GainEffect>(2.0f);
    auto* counter = busEffect.get();
    auto& bus = addBus(*arrangement, 10, 0.5f, 0.25f);
    bus.inserts.slots.push_back(effect(std::move(busEffect)));
    bus.meter = std::make_shared<daw::engine::dsp::PeakLevelMeter>();
    addSend(*arrangement, 0, 20, 10, 1.0f, false);
    addSend(*arrangement, 1, 21, 10, 1.0f, true);
    arrangement->masterGain = 0.5f;

    Mixer mixer;
    mixer.prepare(sampleRate, 512);
    auto state = MixState::startingAt(*arrangement);
    const auto output = render(mixer, *arrangement, state, 0, 2048, 512);

    // Tracks 0.4375; the bus (0.25 + 0.125) x 2 x (0.5, 0.25); then the master x 0.5.
    CHECK(all(output.left, (0.4375f + 0.375f) * 0.5f));
    CHECK(all(output.right, (0.4375f + 0.1875f) * 0.5f));

    // One effect for both tracks, once per sample.
    CHECK(counter->processed == 2048);
    CHECK(bus.meter->takePeak(0) == 0.375f);
    CHECK(bus.meter->takePeak(1) == 0.1875f);
    CHECK(bus.meter->takePeak(0) == 0.0f); // the peak since the previous read
    CHECK(output.heapOperations == 0);
}

TEST_CASE("Send levels and bus faders ramp, and keep their ramps by id", "[engine][bus]")
{
    auto first = tracksOf({1.0f});
    first->tracks[0].leftGain = 0.0f;
    first->tracks[0].rightGain = 0.0f;
    addBus(*first, 10);
    addBus(*first, 11);
    addSend(*first, 0, 20, 10, 0.0f, true);
    addSend(*first, 0, 21, 11, 0.0f, true);

    Mixer mixer;
    mixer.prepare(sampleRate, 512);
    auto state = MixState::startingAt(*first);
    CHECK(all(render(mixer, *first, state, 0, 1024, 512).left, 0.0f));

    // The second arrangement lists buses and sends the other way round, with send 20
    // turned up: its ramp is found by id.
    auto second = tracksOf({1.0f});
    second->tracks[0].leftGain = 0.0f;
    second->tracks[0].rightGain = 0.0f;
    addBus(*second, 11);
    addBus(*second, 10, 0.5f, 0.5f);
    addSend(*second, 0, 21, 11, 0.0f, true);
    addSend(*second, 0, 20, 10, 1.0f, true);

    auto next = MixState::startingAt(*second);
    next.continueFrom(state, *first, *second, mixer.getGainRampLength());
    const auto output = render(mixer, *second, next, 1024, 2048, 100);

    // Send 20 ramps 0 -> 1 over 20 ms and bus 10 ramps 1 -> 0.5 at the same time:
    // (k / 960) x (1 - 0.5 k / 960) at sample k (counting from 1).
    const auto expected = [](int k) {
        const double ramp = k / 960.0;
        return ramp * (1.0 - 0.5 * ramp);
    };
    CHECK_THAT(output.left[0], WithinAbs(expected(1), 1e-6));
    CHECK_THAT(output.left[479], WithinAbs(expected(480), 1e-6));
    CHECK(output.left[rampLength - 1] == 0.5f);
    CHECK(all(output.left, 0.5f, rampLength));
    float largest = 0.0f;
    for (std::size_t i = 1; i < output.left.size(); ++i)
        largest = std::max(largest, std::abs(output.left[i] - output.left[i - 1]));
    CHECK(largest <= 1.001f / rampLength);
    CHECK(output.heapOperations == 0);
}

TEST_CASE("Buses mix the same however the timeline is cut into blocks", "[engine][bus]")
{
    const auto makeSong = [] {
        auto arrangement = tracksOf({0.5f, -0.25f, 0.125f});
        arrangement->tracks[0].leftGain = 0.7f;
        arrangement->tracks[1].rightGain = 0.3f;
        auto& reverb = addBus(*arrangement, 10, 0.9f, 0.8f);
        reverb.inserts.slots.push_back(effect(std::make_unique<LatencyEffect>(333)));
        addBus(*arrangement, 11, 0.6f, 1.1f);
        addSend(*arrangement, 0, 20, 10, 0.5f, false);
        addSend(*arrangement, 1, 21, 10, 0.75f, true);
        addSend(*arrangement, 2, 22, 11, 1.0f, false);
        addSend(*arrangement, 1, 23, 11, 0.3f, false);
        return arrangement;
    };

    const auto renderWith = [&](int blockSize, int busBlockSize) {
        auto first = makeSong();
        auto second = makeSong();
        second->tracks[1].sends[0].gain = 0.1f;
        second->buses[1].leftGain = 0.2f;

        Mixer mixer;
        mixer.prepare(sampleRate, 4096);
        auto state = MixState::startingAt(*first, busBlockSize);
        auto output = render(mixer, *first, state, 0, 6000, blockSize);

        auto next = MixState::startingAt(*second, busBlockSize);
        next.continueFrom(state, *first, *second, mixer.getGainRampLength());
        const auto more = render(mixer, *second, next, 6000, 6000, blockSize);
        output.left.insert(output.left.end(), more.left.begin(), more.left.end());
        output.right.insert(output.right.end(), more.right.begin(), more.right.end());
        output.heapOperations += more.heapOperations;
        return output;
    };

    const auto reference = renderWith(1, 4096);
    const int blockSize = GENERATE(64, 441, 4096);
    const int busBlockSize = GENERATE(4096, 100);
    const auto output = renderWith(blockSize, busBlockSize);

    CAPTURE(blockSize, busBlockSize);
    CHECK(output.left == reference.left);
    CHECK(output.right == reference.right);
    CHECK(output.heapOperations == 0);
}

TEST_CASE("A bus with effects rings on after the transport stops", "[engine][bus]")
{
    auto arrangement = tracksOf({0.5f}, 1000);
    auto& bus = addBus(*arrangement, 10);
    bus.inserts.slots.push_back(effect(std::make_unique<LatencyEffect>(2000)));
    addSend(*arrangement, 0, 20, 10, 1.0f, true);
    arrangement->tracks[0].leftGain = 0.0f; // only the bus is heard: the track's echo
    arrangement->tracks[0].rightGain = 0.0f;

    Engine engine;
    engine.prepare(sampleRate, 256);
    engine.setArrangement(std::move(arrangement));
    engine.play();

    std::vector<float> samples;
    juce::AudioBuffer<float> block(2, 256);
    std::size_t heapOperations = 0;
    for (int i = 0; i < 16; ++i)
    {
        heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
        samples.insert(samples.end(), block.getReadPointer(0), block.getReadPointer(0) + 256);
    }

    CHECK(engine.getTransportState() == daw::engine::TransportState::stopped);
    CHECK(samples[1999] == 0.0f);
    CHECK(samples[2500] == 0.5f);
    CHECK(samples[2999] == 0.5f);
    CHECK(samples[3000] == 0.0f);
    CHECK(heapOperations == 0);
}
