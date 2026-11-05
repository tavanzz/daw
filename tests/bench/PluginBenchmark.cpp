// What hosting a plugin costs on top of the plugin itself: the adapter
// (engine::plugins::PluginEffect) and the host around it (fx::EffectInstance, as the
// mixer runs it).
//
// Build the release preset and run:  build/release/tests/daw_plugin_bench
// Not part of CTest: timings depend on the machine and on what else runs on it.
//
// The VST3 test plugins (tests/plugins: a gain, and a synth whose output shows the
// notes it holds) cost next to nothing themselves, so what is measured is the way
// in and out. For each block size, 10 s of stereo noise at 48 kHz, with denormals
// flushed as on the audio thread, the best of five runs:
//
// 1. JUCE alone: AudioPluginInstance::processBlock(), what any JUCE host pays.
// 2. The adapter: PluginEffect::process() (MIDI conversion, play head, timing).
// 3. The host: EffectInstance::process() over a PluginSlot, as the mixer calls it.
// 4. The host with the gain automated along a ramp: every block cut into 32-sample
//    calls, with a parameter change before each.
// 5. The synth through the host, with eight note events in every block.

#include "daw/engine/fx/EffectInstance.h"
#include "daw/engine/midi/MidiEvent.h"
#include "daw/engine/plugins/PluginEffect.h"
#include "daw/engine/plugins/PluginSlot.h"

#include "support/TestPlugins.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <vector>

namespace
{

using daw::engine::fx::EffectInstance;
using daw::engine::plugins::PluginEffect;
using daw::engine::plugins::PluginSlot;

constexpr double sampleRate = 48000.0;
constexpr int seconds = 10;
constexpr int runs = 5;

// Microseconds per block, the best of the runs: block(i) processes block i.
double timePerBlock(int blockSize, const std::function<void(int)>& block)
{
    const int blocks = static_cast<int>(sampleRate) * seconds / blockSize;
    double best = 1.0e30;
    for (int run = 0; run < runs; ++run)
    {
        const juce::ScopedNoDenormals noDenormals;
        const auto start = std::chrono::steady_clock::now();
        for (int i = 0; i < blocks; ++i)
            block(i);
        const std::chrono::duration<double, std::micro> elapsed = std::chrono::steady_clock::now() - start;
        best = std::min(best, elapsed.count() / blocks);
    }
    return best;
}

std::shared_ptr<PluginEffect> loadPlugin(const juce::String& kind, int blockSize)
{
    auto created = PluginEffect::create(daw::test::loadTestVst3(kind, sampleRate, blockSize));
    if (!std::holds_alternative<std::unique_ptr<PluginEffect>>(created))
    {
        std::fprintf(stderr, "The test plugin %s did not load: build the tests first.\n", kind.toRawUTF8());
        std::exit(1);
    }
    return std::move(std::get<std::unique_ptr<PluginEffect>>(created));
}

void fillNoise(juce::AudioBuffer<float>& buffer)
{
    juce::Random random(1);
    for (int channel = 0; channel < buffer.getNumChannels(); ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(channel, i, random.nextFloat() * 0.5f - 0.25f);
}

struct Row
{
    int blockSize = 0;
    double juceAlone = 0.0;
    double adapter = 0.0;
    double host = 0.0;
    double automated = 0.0;
    double synth = 0.0;
};

Row measure(int blockSize)
{
    Row row;
    row.blockSize = blockSize;
    daw::engine::fx::ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = blockSize;

    juce::AudioBuffer<float> noise(2, blockSize);
    fillNoise(noise);
    juce::AudioBuffer<float> buffer(2, blockSize);

    // 1. JUCE alone.
    {
        auto plugin = daw::test::loadTestVst3("Gain", sampleRate, blockSize);
        plugin->setPlayConfigDetails(2, 2, sampleRate, blockSize);
        plugin->prepareToPlay(sampleRate, blockSize);
        juce::MidiBuffer midi;
        row.juceAlone = timePerBlock(blockSize, [&](int) {
            buffer.makeCopyOf(noise, true);
            plugin->processBlock(buffer, midi);
        });
        plugin->releaseResources();
    }

    // 2. The adapter.
    {
        const auto plugin = loadPlugin("Gain", blockSize);
        plugin->prepare(setup);
        const float last = 0.5f;
        const float* changing = nullptr;
        daw::engine::fx::ProcessBlock block;
        block.numChannels = 2;
        block.numSamples = blockSize;
        block.parameters = daw::engine::fx::ParameterValues(&last, &changing, 1);
        block.time.playing = true;
        row.adapter = timePerBlock(blockSize, [&](int i) {
            buffer.makeCopyOf(noise, true);
            block.channels = buffer.getArrayOfWritePointers();
            block.time.position = static_cast<juce::int64>(i) * blockSize;
            plugin->process(block);
        });
    }

    // 3 and 4. The host, plain and automated.
    for (const bool automate : {false, true})
    {
        const auto plugin = loadPlugin("Gain", blockSize);
        EffectInstance instance(std::make_unique<PluginSlot>(plugin), setup);
        daw::engine::fx::EffectScratch scratch;
        scratch.prepare(blockSize);
        const std::vector<float> values{0.5f};
        daw::engine::fx::SlotSettings settings;
        settings.parameters = values;
        std::vector<float> ramp(static_cast<std::size_t>(blockSize));
        std::vector<const float*> automation{ramp.data()};
        const auto time = timePerBlock(blockSize, [&](int i) {
            buffer.makeCopyOf(noise, true);
            daw::engine::fx::SlotContext context;
            context.time.playing = true;
            context.time.position = static_cast<juce::int64>(i) * blockSize;
            if (automate)
            {
                for (int k = 0; k < blockSize; ++k)
                    ramp[static_cast<std::size_t>(k)] =
                        0.25f + 0.25f * static_cast<float>((i * blockSize + k) % 48000) / 48000.0f;
                context.automation = automation;
            }
            instance.process(buffer.getArrayOfWritePointers(), blockSize, settings, context, scratch);
        });
        (automate ? row.automated : row.host) = time;
    }

    // 5. The synth with eight events a block (four notes struck and released).
    {
        const auto plugin = loadPlugin("Synth", blockSize);
        EffectInstance instance(std::make_unique<PluginSlot>(plugin), setup);
        daw::engine::fx::EffectScratch scratch;
        scratch.prepare(blockSize);
        const std::vector<float> values{0.5f};
        daw::engine::fx::SlotSettings settings;
        settings.parameters = values;
        std::vector<daw::engine::midi::Event> events;
        for (int n = 0; n < 4; ++n)
        {
            const int key = 60 + n;
            events.push_back(daw::engine::midi::noteOn(n * blockSize / 8, key, 0.5f, 0, n));
            events.push_back(daw::engine::midi::noteOff(blockSize / 2 + n * blockSize / 8, key, 0, n));
        }
        std::stable_sort(events.begin(), events.end(), daw::engine::midi::comesBefore);
        row.synth = timePerBlock(blockSize, [&](int i) {
            buffer.clear();
            daw::engine::fx::SlotContext context;
            context.time.playing = true;
            context.time.position = static_cast<juce::int64>(i) * blockSize;
            context.events = events;
            instance.process(buffer.getArrayOfWritePointers(), blockSize, settings, context, scratch);
        });
    }
    return row;
}

} // namespace

int main()
{
    const juce::ScopedJuceInitialiser_GUI juce;
    std::printf("Hosting a plugin, microseconds per block at 48 kHz (the best of %d runs of %d s)\n\n",
                runs, seconds);
    std::printf("| Block | Budget | JUCE alone | Adapter | Host | Host, automated | Synth, 8 events |\n");
    std::printf("|------:|-------:|-----------:|--------:|-----:|----------------:|----------------:|\n");
    for (const int blockSize : {64, 128, 256, 512})
    {
        const auto row = measure(blockSize);
        std::printf("| %d | %.0f us | %.2f | %.2f | %.2f | %.2f | %.2f |\n", row.blockSize,
                    1.0e6 * blockSize / sampleRate, row.juceAlone, row.adapter, row.host, row.automated,
                    row.synth);
    }
    return 0;
}
