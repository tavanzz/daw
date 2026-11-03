#include "daw/engine/AutomationCurve.h"
#include "daw/engine/Engine.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/OfflineRenderer.h"
#include "daw/engine/dsp/FaderLaw.h"
#include "daw/engine/dsp/PanLaw.h"

#include "support/RealtimeCheck.h"
#include "support/TestArrangements.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdint>
#include <memory>
#include <vector>

using Catch::Matchers::WithinAbs;
using daw::engine::Arrangement;
using daw::engine::ArrangementBus;
using daw::engine::ArrangementSend;
using daw::engine::AutomationCurve;
using daw::engine::AutomationPoint;
using daw::engine::ChainSlot;
using daw::engine::Engine;
using daw::engine::Mixer;
using daw::engine::MixState;
using daw::engine::OfflineRenderer;
using daw::engine::dsp::faderGain;
using daw::engine::dsp::faderPosition;
using daw::engine::fx::EffectInstance;
using daw::engine::fx::ProcessSetup;
using daw::test::GainEffect;
using daw::test::LatencyEffect;
using daw::test::ParameterProbe;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int rampLength = 960; // 20 ms

std::shared_ptr<const AutomationCurve> curve(std::vector<AutomationPoint> points)
{
    return std::make_shared<const AutomationCurve>(std::move(points));
}

ChainSlot effect(std::unique_ptr<daw::engine::fx::Effect> effectToHost)
{
    ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = 512;
    ChainSlot slot;
    slot.instance = std::make_shared<EffectInstance>(std::move(effectToHost), setup);
    return slot;
}

std::unique_ptr<Arrangement> constantTrack(float value, int length = 96000)
{
    return daw::test::makeWholeFileArrangement(
        daw::test::makeConstantSource(2, length, value, sampleRate));
}

struct Output
{
    std::vector<float> left;
    std::vector<float> right;
    std::size_t heapOperations = 0;
};

Output render(Mixer& mixer, const Arrangement& arrangement, MixState& state, juce::int64 start,
              int length, const std::vector<int>& pattern)
{
    Output output;
    juce::AudioBuffer<float> block(2, 4096);
    std::size_t index = 0;
    for (int done = 0; done < length;)
    {
        const int piece = std::min(pattern[index++ % pattern.size()], length - done);
        output.heapOperations += daw::test::heapOperationsDuring(
            [&] { mixer.render(arrangement, state, start + done, block, 0, piece); });
        output.left.insert(output.left.end(), block.getReadPointer(0), block.getReadPointer(0) + piece);
        output.right.insert(output.right.end(), block.getReadPointer(1),
                            block.getReadPointer(1) + piece);
        done += piece;
    }
    return output;
}

bool sameBits(float a, float b)
{
    return std::bit_cast<std::uint32_t>(a) == std::bit_cast<std::uint32_t>(b);
}

float largestStep(const std::vector<float>& samples)
{
    float largest = 0.0f;
    for (std::size_t i = 1; i < samples.size(); ++i)
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    return largest;
}

const auto blockPatterns = std::vector<std::vector<int>>{
    {1}, {64}, {4096}, {1, 7, 128, 4096, 33, 1000}};

} // namespace

TEST_CASE("An automation curve goes straight or bent between its points", "[engine][automation]")
{
    const AutomationCurve automation({{1000, 1.0f, 0.0f},
                                      {0, 0.0f, 0.0f},
                                      {2000, 0.5f, 0.5f},
                                      {3000, 0.5f, 0.0f},
                                      {4000, 0.2f, 0.0f},
                                      {4000, 0.8f, 0.0f}});

    CHECK(automation.valueAt(-5) == 0.0f);
    CHECK(automation.valueAt(0) == 0.0f);
    CHECK(automation.valueAt(500) == 0.5f);
    CHECK(automation.valueAt(1000) == 1.0f);
    CHECK(automation.valueAt(1500) ==
          1.0f + (0.5f - 1.0f) * AutomationCurve::shape(0.5, 0.0f));
    CHECK(automation.valueAt(2500) ==
          0.5f + (0.5f - 0.5f) * AutomationCurve::shape(0.5, 0.5f));
    CHECK(automation.valueAt(3999) ==
          0.5f + (0.2f - 0.5f) * AutomationCurve::shape(999.0 / 1000.0, 0.0f));
    CHECK(automation.valueAt(4000) == 0.8f); // a step: the later point holds
    CHECK(automation.valueAt(100000) == 0.8f);

    std::vector<float> values(5000);
    automation.fill(-100, 5000, values.data());
    bool same = true;
    for (int i = 0; i < 5000; ++i)
        same = same && values[static_cast<std::size_t>(i)] == automation.valueAt(i - 100);
    CHECK(same);

    for (const float bend : {-1.0f, -0.5f, 0.5f, 1.0f})
    {
        CAPTURE(bend);
        CHECK(AutomationCurve::shape(0.0, bend) == 0.0f);
        CHECK(AutomationCurve::shape(1.0, bend) == 1.0f);
        float previous = 0.0f;
        bool rising = true;
        for (int i = 1; i <= 100; ++i)
        {
            const float value = AutomationCurve::shape(i / 100.0, bend);
            rising = rising && value > previous;
            previous = value;
        }
        CHECK(rising);
        CHECK((AutomationCurve::shape(0.5, bend) < 0.5f) == (bend > 0.0f));
    }

    CHECK(faderGain(0.0f) == 0.0f);
    CHECK_THAT(faderGain(1.0f), WithinAbs(std::pow(10.0, 6.0 / 20.0), 1e-6));
    CHECK_THAT(faderGain(faderPosition(1.0)), WithinAbs(1.0, 1e-6));
    CHECK(faderPosition(0.0) == 0.0f);
}

TEST_CASE("Volume automation gives exact gains on every sample, whatever the blocks",
          "[engine][automation]")
{
    const auto pattern = GENERATE(Catch::Generators::from_range(blockPatterns));
    auto arrangement = constantTrack(1.0f);
    arrangement->tracks[0].automation.volumeCurve =
        curve({{0, 0.0f, 0.0f}, {20000, 1.0f, 0.7f}, {40000, 0.3f, -0.4f}});

    Mixer mixer;
    mixer.prepare(sampleRate, 4096);
    auto state = MixState::startingAt(*arrangement);
    const auto output = render(mixer, *arrangement, state, 0, 50000, pattern);

    const auto& volume = *arrangement->tracks[0].automation.volumeCurve;
    bool exact = true;
    for (int k = 0; k < 50000; ++k)
    {
        const float expected = faderGain(volume.valueAt(k));
        exact = exact && sameBits(output.left[static_cast<std::size_t>(k)], expected) &&
                sameBits(output.right[static_cast<std::size_t>(k)], expected);
    }
    CAPTURE(pattern);
    CHECK(exact);
    CHECK(output.heapOperations == 0);
}

TEST_CASE("Pan automation moves the track from one side to the other", "[engine][automation]")
{
    auto arrangement = constantTrack(1.0f);
    arrangement->tracks[0].automation.panCurve = curve({{0, 0.0f, 0.0f}, {1000, 1.0f, 0.0f}});

    Mixer mixer;
    mixer.prepare(sampleRate, 4096);
    auto state = MixState::startingAt(*arrangement);
    const auto output = render(mixer, *arrangement, state, 0, 2000, {300});

    const auto& pan = *arrangement->tracks[0].automation.panCurve;
    bool exact = true;
    for (int k = 0; k < 2000; ++k)
    {
        const auto gains = daw::engine::dsp::panGains(2.0 * static_cast<double>(pan.valueAt(k)) - 1.0);
        exact = exact && sameBits(output.left[static_cast<std::size_t>(k)], gains.left) &&
                sameBits(output.right[static_cast<std::size_t>(k)], gains.right);
    }
    CHECK(exact);
    CHECK(output.left.front() > 1.4f);
    CHECK(output.right.front() == 0.0f);
    CHECK(output.left.back() == 0.0f);
}

TEST_CASE("Send levels, bus faders and the master follow their automation",
          "[engine][automation]")
{
    auto arrangement = constantTrack(1.0f);
    arrangement->tracks[0].leftGain = 0.0f; // only the bus is heard
    arrangement->tracks[0].rightGain = 0.0f;
    ArrangementBus bus;
    bus.id = 10;
    bus.automation.volumeCurve = curve({{0, 1.0f, 0.0f}, {3000, 0.5f, 0.0f}});
    arrangement->buses.push_back(bus);
    ArrangementSend send;
    send.id = 20;
    send.bus = 10;
    send.busIndex = 0;
    send.preFader = true;
    send.levelCurve = curve({{0, 0.2f, 0.0f}, {3000, 0.9f, 0.3f}});
    arrangement->tracks[0].sends.push_back(send);
    arrangement->masterCurve = curve({{1000, 0.8f, 0.0f}, {2000, 0.6f, 0.0f}});

    Mixer mixer;
    mixer.prepare(sampleRate, 4096);
    auto state = MixState::startingAt(*arrangement);
    const auto output = render(mixer, *arrangement, state, 0, 4000, {1, 300, 4096});

    const auto& level = *send.levelCurve;
    const auto& busVolume = *bus.automation.volumeCurve;
    const auto& master = *arrangement->masterCurve;
    bool exact = true;
    for (int k = 0; k < 4000; ++k)
    {
        const float expected =
            faderGain(level.valueAt(k)) * faderGain(busVolume.valueAt(k)) * faderGain(master.valueAt(k));
        exact = exact && sameBits(output.left[static_cast<std::size_t>(k)], expected);
    }
    CHECK(exact);
    CHECK(output.heapOperations == 0);
}

TEST_CASE("Effect parameters take their automation on every sample, also while stopped",
          "[engine][automation]")
{
    const auto pattern = GENERATE(Catch::Generators::from_range(blockPatterns));
    auto probe = std::make_unique<ParameterProbe>();
    auto* recorder = probe.get();
    auto arrangement = constantTrack(0.0f);
    auto slot = effect(std::move(probe));
    slot.automation = {curve({{0, 0.0f, 0.0f}, {8000, 1.0f, -0.6f}})};
    arrangement->tracks[0].inserts.slots.push_back(slot);

    Mixer mixer;
    mixer.prepare(sampleRate, 4096);
    auto state = MixState::startingAt(*arrangement);
    const auto output = render(mixer, *arrangement, state, 0, 10000, pattern);

    const auto& automation = *arrangement->tracks[0].inserts.slots[0].automation[0];
    const auto info = recorder->getDescription().parameters[0];
    const auto& level = recorder->recorded[0];
    REQUIRE(level.size() == 10000);
    bool exact = true;
    for (int k = 0; k < 10000; ++k)
        exact = exact && level[static_cast<std::size_t>(k)] == info.fromNormalised(automation.valueAt(k));
    CAPTURE(pattern);
    CHECK(exact);
    CHECK(output.heapOperations == 0);

    // Stopped at 4000: the value there, all along.
    recorder->recorded[0].clear();
    daw::engine::MixContext stopped;
    stopped.timelineRunning = false;
    juce::AudioBuffer<float> block(2, 512);
    mixer.render(*arrangement, state, 4000, block, 0, 512, stopped);
    CHECK(std::all_of(level.begin(), level.end(), [&](float value) {
        return value == info.fromNormalised(automation.valueAt(4000));
    }));
}

TEST_CASE("Automation is read where the audio is: after effects with latency, through loops",
          "[engine][automation][latency]")
{
    // A ramp through an effect of latency 100, then a fader whose automation steps
    // at 2000, while looping [1000, 3000): every sample out has the gain of the
    // timeline position it comes from.
    juce::AudioBuffer<float> rampSamples(2, 10000);
    for (int i = 0; i < 10000; ++i)
        for (int channel = 0; channel < 2; ++channel)
            rampSamples.setSample(channel, i, static_cast<float>(i + 1) / 16384.0f);
    const auto source = daw::test::makeSource(std::move(rampSamples), sampleRate);

    auto arrangement = daw::test::makeWholeFileArrangement(source);
    arrangement->tracks[0].inserts.slots.push_back(effect(std::make_unique<LatencyEffect>(100)));
    const auto volume = curve({{2000, 0.5f, 0.0f}, {2000, 1.0f, 0.0f}});
    arrangement->tracks[0].automation.volumeCurve = volume;
    arrangement->loopStart = 1000;
    arrangement->loopEnd = 3000;

    const int blockSize = GENERATE(64, 441);
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(std::move(arrangement));
    engine.setLooping(true);
    engine.seek(1000);
    engine.play();

    std::vector<float> played;
    juce::AudioBuffer<float> block(2, blockSize);
    std::size_t heapOperations = 0;
    while (played.size() < 7000)
    {
        heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
        played.insert(played.end(), block.getReadPointer(0), block.getReadPointer(0) + blockSize);
    }

    constexpr int fadeLength = 240;
    bool exact = true;
    for (std::size_t s = 0; s < 7000; ++s)
    {
        float expected = 0.0f;
        if (s >= 100)
        {
            const auto j = static_cast<int>(s - 100);       // read j samples after starting
            const juce::int64 position = 1000 + j % 2000;   // where, looping
            float sample = static_cast<float>(position + 1) / 16384.0f;
            if (j < fadeLength)
                sample *= static_cast<float>(j + 1) / static_cast<float>(fadeLength);
            expected = sample * faderGain(volume->valueAt(position));
        }
        exact = exact && sameBits(played[s], expected);
    }
    CAPTURE(blockSize);
    CHECK(exact);
    CHECK(heapOperations == 0);
}

TEST_CASE("The export equals real-time playback with effects and automation",
          "[engine][automation][bounce]")
{
    const auto makeSong = [] {
        juce::AudioBuffer<float> noise(2, 30000);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < 30000; ++i)
                noise.setSample(channel, i, i < 1000 ? 0.0f : 0.3f * daw::test::testInputSample(channel, i));
        const auto source = daw::test::makeSource(std::move(noise), sampleRate);

        auto arrangement = daw::test::makeWholeFileArrangement(source, 1);
        auto second = daw::test::makeWholeFileArrangement(source, 2);
        arrangement->tracks.push_back(second->tracks.front());
        auto& first = arrangement->tracks[0];
        first.inserts.slots.push_back(effect(std::make_unique<LatencyEffect>(64)));
        auto gain = effect(std::make_unique<GainEffect>(1.0f));
        gain.automation = {curve({{0, 0.3f, 0.0f}, {20000, 0.9f, 0.5f}})};
        first.inserts.slots.push_back(gain);
        first.automation.volumeCurve = curve({{5000, 0.9f, 0.0f}, {15000, 0.4f, -0.5f}});
        arrangement->tracks[1].automation.panCurve = curve({{0, 0.0f, 0.0f}, {30000, 1.0f, 0.0f}});

        ArrangementBus bus;
        bus.id = 10;
        bus.inserts.slots.push_back(effect(std::make_unique<LatencyEffect>(20)));
        bus.automation.panCurve = curve({{0, 0.8f, 0.0f}});
        arrangement->buses.push_back(bus);
        ArrangementSend send;
        send.id = 20;
        send.bus = 10;
        send.busIndex = 0;
        send.levelCurve = curve({{10000, 0.2f, 0.0f}, {12000, 0.9f, 0.0f}});
        arrangement->tracks[1].sends.push_back(send);
        arrangement->masterCurve = curve({{25000, 0.85f, 0.0f}, {30000, 0.0f, 0.0f}});
        arrangement->length = 30000;
        return arrangement;
    };

    const auto song = makeSong();
    OfflineRenderer renderer(*song);
    juce::AudioBuffer<float> exported(2, 30000);
    renderer.render(exported);
    const int latency = renderer.getLatency();
    CHECK(latency == 64); // track 1's effect; the bus (20) is faster

    const int blockSize = GENERATE(64, 441);
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(makeSong());
    engine.play();

    std::vector<float> left;
    std::vector<float> right;
    juce::AudioBuffer<float> block(2, blockSize);
    std::size_t heapOperations = 0;
    while (left.size() < static_cast<std::size_t>(30000 + latency))
    {
        heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
        left.insert(left.end(), block.getReadPointer(0), block.getReadPointer(0) + blockSize);
        right.insert(right.end(), block.getReadPointer(1), block.getReadPointer(1) + blockSize);
    }

    bool identical = true;
    for (int k = 0; k < 30000; ++k)
    {
        const auto s = static_cast<std::size_t>(k + latency);
        identical = identical && sameBits(left[s], exported.getSample(0, k)) &&
                    sameBits(right[s], exported.getSample(1, k));
    }
    CAPTURE(blockSize);
    CHECK(identical);
    CHECK(exported.getMagnitude(0, 0, 30000) > 0.05f);
    CHECK(heapOperations == 0);
}

TEST_CASE("Automation that starts or stops glides instead of jumping", "[engine][automation]")
{
    const auto source = daw::test::makeConstantSource(2, 480000, 1.0f, sampleRate);
    Engine engine;
    engine.prepare(sampleRate, 256);
    engine.setArrangement(daw::test::makeWholeFileArrangement(source));
    engine.play();
    juce::AudioBuffer<float> block(2, 256);
    for (int i = 0; i < 4; ++i)
        engine.process(block);

    const auto run = [&](int blocks) {
        std::vector<float> samples;
        for (int i = 0; i < blocks; ++i)
        {
            engine.process(block);
            samples.insert(samples.end(), block.getReadPointer(0), block.getReadPointer(0) + 256);
        }
        return samples;
    };

    // Automation at a quarter of the gain starts: reached over 20 ms.
    auto automated = daw::test::makeWholeFileArrangement(source);
    automated->tracks[0].automation.volumeCurve = curve({{0, faderPosition(0.25), 0.0f}});
    const float target = faderGain(faderPosition(0.25));
    engine.setArrangement(std::move(automated));
    const auto starting = run(8);
    CHECK(starting.front() < 1.0f);
    CHECK(starting.front() > 0.99f);
    CHECK_THAT(starting[rampLength - 1], WithinAbs(target, 1e-6));
    CHECK(starting[rampLength + 100] == target);
    CHECK(largestStep(starting) <= (1.0f - target) / rampLength * 1.01f);

    // It stops (the track's fader is at 0 dB): the gain ramps back.
    engine.setArrangement(daw::test::makeWholeFileArrangement(source));
    const auto stopping = run(8);
    CHECK(stopping[rampLength - 1] == 1.0f);
    CHECK(largestStep(stopping) <= (1.0f - target) / rampLength * 1.01f);
}
