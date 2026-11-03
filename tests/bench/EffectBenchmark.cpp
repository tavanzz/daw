// What each built-in effect costs, and how many tracks with a typical chain fit in
// the time of one audio callback.
//
// Build the release preset and run:  build/release/tests/daw_fx_bench
// Not part of CTest: timings depend on the machine and on what else runs on it.
//
// 1. Each effect, through the host (fx::EffectInstance, as the mixer runs it),
//    processes 10 s of stereo noise in blocks of 256 at 48 kHz, with denormals
//    flushed as on the audio thread: the time per sample and the share of one
//    core it takes in real time. Then 30 s of silence, of which the last 10 s are
//    timed: what an effect costs once its tail has died away.
// 2. Tracks playing noise, each with an equaliser and a compressor and two
//    post-fader sends, to a reverb bus and a delay bus that all tracks share, and
//    a limiter on the master: the most tracks whose 99th-percentile block time
//    stays within the share of the budget asked for (as daw_bench measures, the
//    best of three runs).

#include "daw/engine/Engine.h"
#include "daw/engine/dsp/PanLaw.h"
#include "daw/engine/fx/BuiltInEffects.h"
#include "daw/engine/fx/EffectInstance.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <vector>

namespace
{

using daw::engine::Arrangement;
using daw::engine::ArrangementTrack;
using daw::engine::ChainSlot;
using daw::engine::Engine;
using daw::engine::fx::EffectInstance;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr int warmUpBlocks = 100;
constexpr int measuredBlocks = 600;

using Values = std::map<juce::String, float>;

std::shared_ptr<EffectInstance> makeInstance(const juce::String& type)
{
    daw::engine::fx::ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = 512;
    return std::make_shared<EffectInstance>(daw::engine::fx::createBuiltInEffect(type), setup);
}

std::vector<float> normalisedValues(const EffectInstance& instance, const Values& values)
{
    std::vector<float> normalised;
    for (const auto& info : instance.getDescription().parameters)
    {
        const auto found = values.find(info.id);
        normalised.push_back(info.toNormalised(found != values.end() ? found->second : info.defaultValue));
    }
    return normalised;
}

ChainSlot makeSlot(const juce::String& type, const Values& values)
{
    ChainSlot slot;
    slot.instance = makeInstance(type);
    slot.parameters = normalisedValues(*slot.instance, values);
    return slot;
}

// An equaliser working on every band.
const Values busyEqualiser{{"highPassOn", 1.0f},  {"lowPassOn", 1.0f},  {"lowShelfGain", 3.0f},
                           {"bell1Gain", -4.0f},  {"bell2Gain", 2.0f},  {"bell3Gain", -3.0f},
                           {"bell4Gain", 5.0f},   {"highShelfGain", -2.0f}};
const Values workingCompressor{{"threshold", -40.0f}, {"ratio", 4.0f}};

struct Case
{
    const char* name;
    juce::String type;
    Values values;
    bool sweep = false; // automate the first bell's frequency, every sample
};

struct Cost
{
    double nanosecondsPerSample = 0.0;
    double coreShare = 0.0; // of one core, in real time
};

Cost time(const Case& test, const juce::AudioBuffer<float>& input, int skipSeconds)
{
    const juce::ScopedNoDenormals noDenormals;
    auto instance = makeInstance(test.type);
    daw::engine::fx::EffectScratch scratch;
    scratch.prepare(blockSize);
    const auto targets = normalisedValues(*instance, test.values);
    daw::engine::fx::SlotSettings settings;
    settings.parameters = targets;

    const int index = instance->getDescription().indexOf("bell1Frequency");
    std::vector<float> sweep(static_cast<std::size_t>(blockSize));
    std::vector<const float*> automation(instance->getDescription().parameters.size(), nullptr);

    juce::AudioBuffer<float> block(2, blockSize);
    const int total = input.getNumSamples();
    const int skipped = skipSeconds * static_cast<int>(sampleRate);
    double seconds = 0.0;

    for (int done = 0; done < total; done += blockSize)
    {
        const int length = std::min(blockSize, total - done);
        for (int channel = 0; channel < 2; ++channel)
            block.copyFrom(channel, 0, input, channel, done, length);

        daw::engine::fx::SlotContext context;
        context.time.playing = true;
        if (test.sweep && index >= 0)
        {
            for (int i = 0; i < length; ++i)
                sweep[static_cast<std::size_t>(i)] =
                    static_cast<float>((done + i) % 48000) / 48000.0f;
            automation[static_cast<std::size_t>(index)] = sweep.data();
            context.automation = automation;
        }

        float* const channels[] = {block.getWritePointer(0), block.getWritePointer(1)};
        const auto start = std::chrono::steady_clock::now();
        instance->process(channels, length, settings, context, scratch);
        const auto end = std::chrono::steady_clock::now();
        if (done >= skipped)
            seconds += std::chrono::duration<double>(end - start).count();
    }

    const double samples = total - skipped;
    return {seconds * 1.0e9 / samples, seconds / (samples / sampleRate)};
}

juce::AudioBuffer<float> noise(int seconds, float amplitude, int seed)
{
    juce::AudioBuffer<float> buffer(2, seconds * static_cast<int>(sampleRate));
    juce::Random random(seed);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < buffer.getNumSamples(); ++i)
            buffer.setSample(channel, i, (random.nextFloat() * 2.0f - 1.0f) * amplitude);
    return buffer;
}

// Tracks with a typical chain, sends to two shared buses, a limiter on the master.
std::unique_ptr<Arrangement> makeSong(int numTracks,
                                      const std::vector<std::shared_ptr<const daw::engine::MemoryAudioSource>>& sources)
{
    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = sampleRate;
    arrangement->length = 48000;

    daw::engine::ArrangementBus reverb;
    reverb.id = 1;
    reverb.inserts.slots.push_back(makeSlot("reverb", {{"mix", 100.0f}}));
    daw::engine::ArrangementBus delay;
    delay.id = 2;
    delay.inserts.slots.push_back(makeSlot("delay", {{"mix", 100.0f}}));
    arrangement->buses = {reverb, delay};
    arrangement->masterInserts.slots.push_back(makeSlot("limiter", {}));

    for (int t = 0; t < numTracks; ++t)
    {
        const auto& source = sources[static_cast<std::size_t>(t) % sources.size()];
        const auto pan = daw::engine::dsp::panGains(static_cast<double>(t % 21) / 10.0 - 1.0);
        ArrangementTrack track;
        track.id = 100 + t;
        track.leftGain = 0.5f * pan.left;
        track.rightGain = 0.5f * pan.right;
        track.segments.push_back({source.get(), 0, 48000, 0, 0, 0});
        track.inserts.slots.push_back(makeSlot("equaliser", busyEqualiser));
        track.inserts.slots.push_back(makeSlot("compressor", workingCompressor));
        for (const juce::int64 bus : {1, 2})
        {
            daw::engine::ArrangementSend send;
            send.id = 10000 + 2 * t + bus;
            send.bus = bus;
            send.busIndex = static_cast<int>(bus - 1);
            send.gain = 0.3f;
            track.sends.push_back(send);
        }
        arrangement->tracks.push_back(std::move(track));
    }

    arrangement->sources.assign(sources.begin(), sources.end());
    return arrangement;
}

double p99(int numTracks, int block,
           const std::vector<std::shared_ptr<const daw::engine::MemoryAudioSource>>& sources)
{
    Engine engine;
    engine.prepare(sampleRate, block);
    engine.setArrangement(makeSong(numTracks, sources));
    engine.setLooping(true);
    engine.play();

    juce::AudioBuffer<float> buffer(2, block);
    std::vector<double> times;
    for (int i = 0; i < warmUpBlocks + measuredBlocks; ++i)
    {
        const auto start = std::chrono::steady_clock::now();
        engine.process(buffer);
        const auto end = std::chrono::steady_clock::now();
        if (i >= warmUpBlocks)
            times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }
    std::sort(times.begin(), times.end());
    return times[times.size() * 99 / 100];
}

int mostTracksWithin(double share, int block,
                     const std::vector<std::shared_ptr<const daw::engine::MemoryAudioSource>>& sources)
{
    // The best of three runs: one stray spike of the scheduler (the benchmark runs at
    // normal priority) should not decide.
    const double budgetMs = 1000.0 * block / sampleRate * share;
    const auto fits = [&](int tracks) {
        double best = p99(tracks, block, sources);
        for (int run = 1; run < 3 && best > budgetMs; ++run)
            best = std::min(best, p99(tracks, block, sources));
        return best <= budgetMs;
    };

    int low = 0;
    int high = 8;
    while (high <= 4096 && fits(high))
    {
        low = high;
        high *= 2;
    }
    while (high - low > std::max(1, low / 50))
    {
        const int middle = low + (high - low) / 2;
        if (fits(middle))
            low = middle;
        else
            high = middle;
    }
    return low;
}

} // namespace

int main()
{
#if !defined(NDEBUG)
    std::printf("Warning: this is not an optimised build; use the release preset.\n\n");
#endif

    const std::vector<Case> cases{
        {"Equaliser, neutral (bands on at 0 dB)", "equaliser", {}},
        {"Equaliser, every band working", "equaliser", busyEqualiser},
        {"Equaliser, a bell's frequency automated", "equaliser", busyEqualiser, true},
        {"Compressor", "compressor", workingCompressor},
        {"Limiter", "limiter", {{"inputGain", 12.0f}}},
        {"Delay (350 ms, 35 %)", "delay", {{"mix", 100.0f}}},
        {"Reverb (2 s)", "reverb", {{"mix", 100.0f}}},
    };

    const auto signal = noise(10, 0.25f, 1);
    auto thenSilence = noise(10, 0.25f, 2);
    thenSilence.setSize(2, 40 * 48000, true, true);

    std::printf("Cost of each effect, stereo at 48 kHz, blocks of 256\n\n");
    std::printf("| Effect | Signal: ns/sample | Share of a core | Silence: ns/sample |\n");
    std::printf("|--------|------------------:|----------------:|-------------------:|\n");
    for (const auto& test : cases)
    {
        const auto busy = time(test, signal, 0);
        const auto quiet = time(test, thenSilence, 30);
        std::printf("| %s | %.1f | %.2f %% | %.1f |\n", test.name, busy.nanosecondsPerSample,
                    100.0 * busy.coreShare, quiet.nanosecondsPerSample);
        std::fflush(stdout);
    }

    std::vector<std::shared_ptr<const daw::engine::MemoryAudioSource>> sources;
    for (int s = 0; s < 64; ++s)
    {
        daw::engine::AudioData data;
        data.sampleRate = sampleRate;
        data.samples = noise(1, 0.1f, 100 + s);
        sources.push_back(std::make_shared<const daw::engine::MemoryAudioSource>(std::move(data)));
    }

    std::printf("\nTracks with an equaliser, a compressor and two sends (reverb and delay buses, "
                "a limiter on the master)\n\n");
    std::printf("| Block | Budget | Within 100%% | Within 50%% |\n");
    std::printf("|------:|-------:|------------:|-----------:|\n");
    for (const int block : {128, 256, 512})
    {
        const int full = mostTracksWithin(1.0, block, sources);
        const int half = mostTracksWithin(0.5, block, sources);
        std::printf("| %5d | %4.2f ms | %11d | %10d |\n", block, 1000.0 * block / sampleRate, full,
                    half);
        std::fflush(stdout);
    }
    return 0;
}
