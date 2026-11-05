#include "daw/engine/Engine.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/plugins/PluginEffect.h"

#include "support/MidiArrangements.h"
#include "support/RealtimeCheck.h"
#include "support/SimulatedDevice.h"
#include "support/TestArrangements.h"
#include "support/TestPlugins.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/generators/catch_generators_range.hpp>

#include <cmath>
#include <memory>
#include <vector>

namespace midi = daw::engine::midi;
using daw::engine::Arrangement;
using daw::engine::ArrangementTrack;
using daw::engine::AutomationCurve;
using daw::engine::ChainSlot;
using daw::engine::Engine;
using daw::engine::Mixer;
using daw::engine::MixState;
using daw::engine::fx::EffectInstance;
using daw::engine::fx::ProcessSetup;
using daw::engine::plugins::PluginEffect;

namespace
{

constexpr double sampleRate = 48000.0;

std::vector<std::string> formatNames()
{
    std::vector<std::string> names;
    for (const auto& format : daw::test::testPluginFormats())
        names.push_back(format.toStdString());
    return names;
}
constexpr int maximumBlock = 4096;

std::unique_ptr<PluginEffect> makePlugin(const juce::String& format, const juce::String& kind)
{
    auto instance = daw::test::loadTestPlugin(format, kind, sampleRate, maximumBlock);
    REQUIRE(instance != nullptr);
    auto created = PluginEffect::create(std::move(instance));
    REQUIRE(std::holds_alternative<std::unique_ptr<PluginEffect>>(created));
    return std::move(std::get<std::unique_ptr<PluginEffect>>(created));
}

std::shared_ptr<EffectInstance> host(std::unique_ptr<PluginEffect> plugin, bool fadeIn = false)
{
    ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = maximumBlock;
    return std::make_shared<EffectInstance>(std::move(plugin), setup, fadeIn);
}

ChainSlot slotOf(std::shared_ptr<EffectInstance> instance)
{
    ChainSlot slot;
    slot.instance = std::move(instance);
    return slot;
}

std::shared_ptr<const daw::engine::MemoryAudioSource> constant(float value, int length)
{
    juce::AudioBuffer<float> samples(2, length);
    for (int channel = 0; channel < 2; ++channel)
        juce::FloatVectorOperations::fill(samples.getWritePointer(channel), value, length);
    return daw::test::makeSource(std::move(samples), sampleRate);
}

std::shared_ptr<const daw::engine::MemoryAudioSource> impulseAt(float value, int position, int length)
{
    juce::AudioBuffer<float> samples(2, length);
    samples.clear();
    samples.setSample(0, position, value);
    samples.setSample(1, position, value);
    return daw::test::makeSource(std::move(samples), sampleRate);
}

void addTrack(Arrangement& arrangement, juce::int64 id,
              const std::shared_ptr<const daw::engine::MemoryAudioSource>& source)
{
    ArrangementTrack track;
    track.id = id;
    track.segments.push_back({source.get(), 0, source->getLength(), 0, 0, 0});
    arrangement.tracks.push_back(std::move(track));
    arrangement.sources.push_back(source);
    arrangement.length = std::max(arrangement.length, source->getLength());
}

struct Rendered
{
    std::vector<float> left;
    std::vector<float> right;
    std::size_t heapOperations = 0;
};

Rendered render(const Arrangement& arrangement, int length, const std::vector<int>& pattern)
{
    Mixer mixer;
    mixer.prepare(sampleRate, maximumBlock);
    auto state = MixState::startingAt(arrangement);
    Rendered rendered;
    juce::AudioBuffer<float> block(2, maximumBlock);
    std::size_t index = 0;
    for (int done = 0; done < length;)
    {
        const int piece = std::min(pattern[index++ % pattern.size()], length - done);
        rendered.heapOperations += daw::test::heapOperationsDuring(
            [&] { mixer.render(arrangement, state, done, block, 0, piece); });
        rendered.left.insert(rendered.left.end(), block.getReadPointer(0), block.getReadPointer(0) + piece);
        rendered.right.insert(rendered.right.end(), block.getReadPointer(1), block.getReadPointer(1) + piece);
        done += piece;
    }
    return rendered;
}

} // namespace

TEST_CASE("A plugin gain at unity leaves the mix bit-identical to an empty chain",
          "[engine][plugins]")
{
    const auto format = juce::String(GENERATE(from_range(formatNames())));
    const auto pattern = GENERATE(std::vector<int>{512}, std::vector<int>{1, 7, 100, 333, 4096});
    CAPTURE(format.toStdString(), pattern);

    juce::AudioBuffer<float> noise(2, 20000);
    juce::Random random(11);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample(channel, i, random.nextFloat() * 2.0f - 1.0f);
    const auto source = daw::test::makeSource(noise, sampleRate);

    Arrangement plain;
    plain.sampleRate = sampleRate;
    addTrack(plain, 1, source);
    Arrangement withPlugin;
    withPlugin.sampleRate = sampleRate;
    addTrack(withPlugin, 1, source);
    withPlugin.tracks[0].inserts.slots.push_back(slotOf(host(makePlugin(format, "Gain"))));

    const auto expected = render(plain, 20000, {512});
    const auto actual = render(withPlugin, 20000, pattern);
    CHECK(actual.left == expected.left);
    CHECK(actual.right == expected.right);
    CHECK(actual.heapOperations == 0); // the plugin's own code is left out (rt::ForeignCode)
}

TEST_CASE("A plugin's declared latency is compensated to the sample", "[engine][plugins][latency]")
{
    const auto format = juce::String(GENERATE(from_range(formatNames())));
    CAPTURE(format.toStdString());

    Arrangement arrangement;
    arrangement.sampleRate = sampleRate;
    addTrack(arrangement, 1, impulseAt(0.25f, 1000, 4000));
    addTrack(arrangement, 2, impulseAt(0.5f, 1000, 4000));
    const auto latency = host(makePlugin(format, "Latency"));
    REQUIRE(latency->getLatency() == 777); // as the plugin reports it once prepared
    arrangement.tracks[0].inserts.slots.push_back(slotOf(latency));

    const auto state = MixState::startingAt(arrangement);
    CHECK(state.latency == 777);
    const auto rendered = render(arrangement, 4000, {128});
    for (std::size_t i = 0; i < rendered.left.size(); ++i)
        REQUIRE(rendered.left[i] == (i == 1777 ? 0.75f : 0.0f));
    CHECK(rendered.heapOperations == 0);
}

TEST_CASE("Automation reaches a plugin on its sample at points and every 32 samples on ramps",
          "[engine][plugins][automation]")
{
    const auto format = juce::String(GENERATE(from_range(formatNames())));
    CAPTURE(format.toStdString());

    // Gain 1 (0.5 normalised) to 0.5 (0.25) as a step at 1001, back to 1 at 2000, then a
    // ramp from 1 down to 0 over [3000, 4000).
    const auto curve = std::make_shared<AutomationCurve>(std::vector<daw::engine::AutomationPoint>{
        {0, 0.5f, 0.0f}, {1001, 0.5f, 0.0f}, {1001, 0.25f, 0.0f}, {2000, 0.25f, 0.0f},
        {2000, 0.5f, 0.0f}, {3000, 0.5f, 0.0f}, {4000, 0.0f, 0.0f}});

    std::vector<float> first;
    for (const auto& pattern : {std::vector<int>{512}, std::vector<int>{1, 7, 100, 333, 4096}})
    {
        Arrangement arrangement;
        arrangement.sampleRate = sampleRate;
        addTrack(arrangement, 1, constant(1.0f, 5000));
        auto slot = slotOf(host(makePlugin(format, "Gain")));
        slot.automation = {curve};
        arrangement.tracks[0].inserts.slots.push_back(std::move(slot));

        const auto rendered = render(arrangement, 5000, pattern);
        const auto& out = rendered.left;
        CHECK(out[1000] == 1.0f);
        CHECK(out[1001] == 0.5f); // a step, on its sample
        CHECK(out[1999] == 0.5f);
        CHECK(out[2000] == 1.0f);
        CHECK(out[3000] == 1.0f);
        // Along the ramp, the curve's value at each multiple of 32 (of the timeline),
        // held until the next.
        for (int k = 3008; k < 4000; ++k)
        {
            const int grid = k - k % PluginEffect::automationGrid;
            REQUIRE(out[static_cast<std::size_t>(k)] == 2.0f * curve->valueAt(grid));
        }
        CHECK(out[4500] == 0.0f);
        CHECK(rendered.heapOperations == 0);

        // The same whatever the blocks.
        if (first.empty())
            first = out;
        else
            CHECK(out == first);
    }
}

TEST_CASE("A plugin instrument gets its notes on their samples and holds none after stop, jumps, loops, previews and Panic",
          "[engine][plugins][midi]")
{
    const auto format = juce::String(GENERATE(from_range(formatNames())));
    CAPTURE(format.toStdString());

    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = sampleRate;
    arrangement->length = 48000;
    ArrangementTrack track;
    track.id = 1;
    track.playsNotes = true;
    // MIDI 1.0 velocities (7 bits) reach every format exactly.
    track.notes = {daw::test::makeNote(100, 300, 60, 1.0f, 1),
                   daw::test::makeNote(250, 4000, 64, midi::fromSevenBit(64), 2),
                   daw::test::makeNote(5000, 30000, 67, midi::fromSevenBit(32), 3)};
    track.inserts.slots.push_back(slotOf(host(makePlugin(format, "Synth"))));
    arrangement->tracks.push_back(std::move(track));

    SECTION("on their samples, whatever the blocks")
    {
        const auto pattern = GENERATE(std::vector<int>{256}, std::vector<int>{1, 7, 100, 333, 4096});
        const auto rendered = render(*arrangement, 6000, pattern);
        const auto& out = rendered.left;
        CHECK(out[99] == 0.0f);
        CHECK(out[100] == 1.0f);
        const float second = 64.0f / 127.0f;
        CHECK(out[250] == 1.0f + second);
        CHECK(out[299] == 1.0f + second);
        CHECK(out[300] == second);
        CHECK(out[3999] == second);
        CHECK(out[4000] == 0.0f);
        CHECK(out[5000] == 32.0f / 127.0f);
        CHECK(rendered.right[5000] == 1.0f); // one note held
        CHECK(rendered.heapOperations == 0);
    }

    SECTION("through the engine: stop, jump, loop, preview, Panic")
    {
        Engine engine;
        daw::test::SimulatedDevice device(engine, sampleRate, 0, {256});
        device.setRecordsOutput(true);
        device.start();
        engine.setArrangement(std::move(arrangement));
        const auto heldNow = [&] { return device.getOutput(1).back(); };

        engine.play();
        REQUIRE(device.run(6000) == 0);
        REQUIRE(heldNow() == 1.0f); // the long note
        engine.stop();
        REQUIRE(device.run(1024) == 0);
        CHECK(heldNow() == 0.0f);

        engine.play();
        REQUIRE(device.run(6000) == 0);
        engine.seek(200); // a jump: the long note ends; no note chase
        REQUIRE(device.run(512) == 0);
        CHECK(heldNow() == 1.0f); // E starts after the jump target; C began before it (no chase)
        engine.stop();
        REQUIRE(device.run(1024) == 0);
        CHECK(heldNow() == 0.0f);

        engine.startPreview(1, 72, 0.5f);
        REQUIRE(device.run(512) == 0);
        CHECK(heldNow() == 1.0f);
        engine.stopPreview(1, 72);
        REQUIRE(device.run(512) == 0);
        CHECK(heldNow() == 0.0f);

        engine.startPreview(1, 72, 0.5f);
        engine.play();
        REQUIRE(device.run(6000) == 0);
        engine.panic();
        REQUIRE(device.run(512) == 0);
        CHECK(heldNow() == 0.0f);
        engine.stopPreview(1, 72);
        engine.stop();
        REQUIRE(device.run(1024) == 0);
        CHECK(heldNow() == 0.0f);
        engine.collectGarbage();
    }
}

TEST_CASE("A plugin that outputs NaN is silenced and reported", "[engine][plugins]")
{
    const auto format = juce::String(GENERATE(from_range(formatNames())));
    CAPTURE(format.toStdString());

    Arrangement arrangement;
    arrangement.sampleRate = sampleRate;
    addTrack(arrangement, 1, constant(0.5f, 4000));
    const auto instance = host(makePlugin(format, "NaN"));
    arrangement.tracks[0].inserts.slots.push_back(slotOf(instance));

    const auto rendered = render(arrangement, 4000, {256});
    for (const float sample : rendered.left)
        REQUIRE(sample == 0.0f);
    CHECK(instance->hasFailed());
}

TEST_CASE("A plugin's state, parameters and own changes", "[engine][plugins]")
{
    const auto format = juce::String(GENERATE(from_range(formatNames())));
    CAPTURE(format.toStdString());
    auto plugin = makePlugin(format, "Gain");

    const auto& description = plugin->getDescription();
    CHECK(description.type == "plugin");
    CHECK(description.name == "DAW Test Gain");
    CHECK_FALSE(description.isInstrument);
    REQUIRE_FALSE(description.parameters.empty());
    CHECK(description.parameters[0].name.containsIgnoreCase("gain"));
    CHECK_FALSE(description.parameters[0].smoothed);
    CHECK(plugin->getLayout() == daw::engine::plugins::PluginLayout{2, 2});

    ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = 512;
    plugin->prepare(setup);

    // What the host sets is not reported back as the plugin's own change.
    std::vector<PluginEffect::ParameterReport> reports;
    plugin->setParameter(0, 0.25f);
    CHECK(plugin->getParameter(0) == 0.25f);
    plugin->takeParameterReports(reports);
    CHECK(reports.empty());

    // The state carries the parameter (once the plugin has run: LV2 applies values
    // to its ports there).
    {
        juce::AudioBuffer<float> audio(2, 64);
        audio.clear();
        daw::engine::fx::ProcessBlock block;
        block.channels = audio.getArrayOfWritePointers();
        block.numChannels = 2;
        block.numSamples = 64;
        plugin->process(block);
    }
    const auto state = plugin->getExtraState();
    CHECK_FALSE(state.isEmpty());
    auto other = makePlugin(format, "Gain");
    CHECK(other->getParameter(0) == 0.5f);
    REQUIRE(other->setExtraState(state));
    CHECK(other->getParameter(0) == 0.25f);

    // What the plugin changes itself (here through JUCE, as an editor would) is.
    auto& parameter = *plugin->getPlugin().getParameters()[0];
    parameter.beginChangeGesture();
    parameter.setValueNotifyingHost(0.75f);
    plugin->takeParameterReports(reports);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].index == 0);
    CHECK(reports[0].value == 0.75f);
    CHECK(reports[0].inGesture);
    parameter.setValueNotifyingHost(0.8f);
    parameter.endChangeGesture();
    plugin->takeParameterReports(reports);
    REQUIRE(reports.size() == 1);
    CHECK(reports[0].value == 0.8f);
    CHECK_FALSE(reports[0].inGesture);
    CHECK(reports[0].gestureEnded);
    plugin->takeParameterReports(reports);
    CHECK(reports.empty());

    CHECK(plugin->getParameterText(0, 0.5f).isNotEmpty());
}
