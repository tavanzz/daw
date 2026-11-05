// What each built-in instrument costs per voice, and how many voices fit in the time
// of one audio callback.
//
// Build the release preset and run:  build/release/tests/daw_instrument_bench [name]
// (a name, or part of one, runs only those instruments).
// Not part of CTest: timings depend on the machine and on what else runs on it.
//
// 1. Each instrument, through the host (fx::EffectInstance, as the mixer runs it),
//    holds 0, 1, 8 and 16 notes on different keys (the drum sampler has 16 pads, each
//    decaying over 10 s here) for 10 s in blocks of 256 at 48 kHz, with denormals
//    flushed as on the audio thread.
//    The cost of a voice is the time with 16 voices less the time with none, over 16:
//    in nanoseconds per sample, and how many voices one core plays in real time.
// 2. Tracks, each an instrument holding 16 notes, played by the engine: the most
//    tracks whose 99th-percentile block time stays within the share of the budget
//    asked for (as daw_bench measures, the best of three runs), times 16 voices, and
//    the median block time there; at blocks of 128, 256 and 512 for the synth's
//    default sound, 256 for the others.

#include "daw/engine/Engine.h"
#include "daw/engine/Sample.h"
#include "daw/engine/fx/BuiltInEffects.h"
#include "daw/engine/fx/DrumSampler.h"
#include "daw/engine/fx/EffectInstance.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <chrono>
#include <cmath>
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
using daw::engine::Sample;
using daw::engine::fx::EffectInstance;
namespace midi = daw::engine::midi;

constexpr double sampleRate = 48000.0;
constexpr int blockSize = 256;
constexpr int warmUpBlocks = 100;
constexpr int measuredBlocks = 600;
constexpr int voicesPerTrack = 16;

using Values = std::map<juce::String, float>;

struct Case
{
    const char* name;
    juce::String type;
    Values values;
};

std::shared_ptr<const Sample> makeSample(double seconds, int seed)
{
    const auto length = static_cast<int>(seconds * sampleRate);
    juce::AudioBuffer<float> audio(2, length);
    juce::Random random(seed);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < length; ++i)
            audio.setSample(channel, i,
                            0.3f * static_cast<float>(std::sin(i * 0.01 * (channel + 1))) +
                                0.05f * (random.nextFloat() - 0.5f));
    return Sample::fromAudio(std::move(audio), sampleRate);
}

std::shared_ptr<EffectInstance> makeInstance(const juce::String& type, int maximumBlock)
{
    daw::engine::fx::ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = maximumBlock;
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

// The samples an instrument's slots play: one long sample for each, made once and
// shared by every track (as a cache shares one file's sample).
std::vector<std::shared_ptr<const Sample>> samplesFor(const EffectInstance& instance)
{
    static std::vector<std::shared_ptr<const Sample>> made;
    std::vector<std::shared_ptr<const Sample>> samples;
    for (std::size_t i = 0; i < instance.getDescription().sampleSlots.size(); ++i)
    {
        if (i == made.size())
            made.push_back(makeSample(12.0, static_cast<int>(i) + 1));
        samples.push_back(made[i]);
    }
    return samples;
}

// The keys held: for drums one per pad, otherwise spread around middle C.
int keyOf(const Case& test, int voice)
{
    if (test.type == "drums")
        return daw::engine::fx::DrumSampler::firstKey + voice % daw::engine::fx::DrumSampler::numPads;
    return 36 + (voice * 7) % 48;
}

double secondsHolding(const Case& test, int voices)
{
    const juce::ScopedNoDenormals noDenormals;
    auto instance = makeInstance(test.type, blockSize);
    daw::engine::fx::EffectScratch scratch;
    scratch.prepare(blockSize);
    const auto targets = normalisedValues(*instance, test.values);
    const auto samples = samplesFor(*instance);
    std::vector<const Sample*> pointers;
    for (const auto& sample : samples)
        pointers.push_back(sample.get());

    daw::engine::fx::SlotSettings settings;
    settings.parameters = targets;
    settings.samples = pointers;

    std::vector<midi::Event> notes;
    for (int voice = 0; voice < voices; ++voice)
        notes.push_back(midi::noteOn(0, keyOf(test, voice), 0.8f, voice / 128, voice + 1));

    juce::AudioBuffer<float> block(2, blockSize);
    const int total = 10 * static_cast<int>(sampleRate);
    double seconds = 0.0;
    for (int done = 0; done < total; done += blockSize)
    {
        block.clear();
        daw::engine::fx::SlotContext context;
        context.time.playing = true;
        if (done == 0)
            context.events = notes;
        float* const channels[] = {block.getWritePointer(0), block.getWritePointer(1)};
        const auto start = std::chrono::steady_clock::now();
        instance->process(channels, blockSize, settings, context, scratch);
        const auto end = std::chrono::steady_clock::now();
        if (done >= static_cast<int>(sampleRate)) // after a second, all attacks are over
            seconds += std::chrono::duration<double>(end - start).count();
    }
    return seconds / (total - static_cast<int>(sampleRate));
}

std::unique_ptr<Arrangement> makeSong(const Case& test, int numTracks, int block)
{
    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = sampleRate;
    arrangement->length = 100 * static_cast<juce::int64>(sampleRate);

    for (int t = 0; t < numTracks; ++t)
    {
        ArrangementTrack track;
        track.id = 100 + t;
        track.leftGain = 0.1f;
        track.rightGain = 0.1f;
        track.playsNotes = true;
        for (int voice = 0; voice < voicesPerTrack; ++voice)
        {
            const auto key = static_cast<std::uint8_t>(keyOf(test, voice + t));
            track.notes.push_back({voice, arrangement->length, key, 0, 0.8f,
                                   static_cast<std::uint64_t>(voice + 1)});
        }

        ChainSlot slot;
        slot.instance = makeInstance(test.type, block);
        slot.parameters = normalisedValues(*slot.instance, test.values);
        slot.samples = samplesFor(*slot.instance);
        for (const auto& sample : slot.samples)
            slot.samplePointers.push_back(sample.get());
        track.inserts.slots.push_back(std::move(slot));
        arrangement->tracks.push_back(std::move(track));
    }
    return arrangement;
}

struct BlockTimes
{
    double median = 0.0;
    double p99 = 0.0;
};

BlockTimes blockTimes(const Case& test, int numTracks, int block)
{
    Engine engine;
    engine.prepare(sampleRate, block);
    engine.setArrangement(makeSong(test, numTracks, block));
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
    engine.collectGarbage();
    std::sort(times.begin(), times.end());
    return {times[times.size() / 2], times[times.size() * 99 / 100]};
}

struct Fit
{
    int tracks = 0;
    double median = 0.0; ///< of the block times at that many tracks, in ms
};

Fit mostTracksWithin(const Case& test, double share, int block)
{
    const double budgetMs = 1000.0 * block / sampleRate * share;
    double medianAtLow = 0.0;
    const auto fits = [&](int tracks) {
        auto best = blockTimes(test, tracks, block);
        for (int run = 1; run < 3 && best.p99 > budgetMs; ++run)
        {
            const auto again = blockTimes(test, tracks, block);
            if (again.p99 < best.p99)
                best = again;
        }
        const bool within = best.p99 <= budgetMs;
        if (within)
            medianAtLow = best.median;
        return within;
    };

    int low = 0;
    double medianOfLow = 0.0;
    int high = 1;
    while (high <= 1024 && fits(high))
    {
        low = high;
        medianOfLow = medianAtLow;
        high *= 2;
    }
    while (high - low > std::max(1, low / 50))
    {
        const int middle = low + (high - low) / 2;
        if (fits(middle))
        {
            low = middle;
            medianOfLow = medianAtLow;
        }
        else
        {
            high = middle;
        }
    }
    return {low, medianOfLow};
}

// Every pad decaying over 10 s, so that its voices sound through the measurement.
Values longDrums()
{
    Values values;
    for (int pad = 1; pad <= daw::engine::fx::DrumSampler::numPads; ++pad)
        values["pad" + juce::String(pad) + "Decay"] = 10000.0f;
    return values;
}

} // namespace

int main(int argc, char* argv[])
{
#if !defined(NDEBUG)
    std::printf("Warning: this is not an optimised build; use the release preset.\n\n");
#endif

    const std::vector<Case> cases{
        {"Synth, default (two saws, low-pass 24)", "synth", {{"polyphony", 31.0f}}},
        {"Synth, busy (two pulses, noise, LFO on pitch and cutoff)",
         "synth",
         {{"polyphony", 31.0f}, {"osc1Wave", 3.0f}, {"osc2Wave", 3.0f}, {"noiseLevel", 30.0f},
          {"lfoToCutoff", 24.0f}, {"lfoToPitch", 20.0f}}},
        {"Sampler (stereo, looped with a crossfade)",
         "sampler",
         {{"polyphony", 31.0f}, {"loop", 1.0f}, {"loopStart", 10.0f}, {"loopEnd", 90.0f},
          {"crossfade", 20.0f}}},
        {"Drum sampler (stereo one-shots)", "drums", longDrums()},
    };

    // An argument runs only the cases whose name contains it.
    const juce::String only = argc > 1 ? juce::String(argv[1]) : juce::String();
    const auto wanted = [&only](const Case& test) { return only.isEmpty() || juce::String(test.name).contains(only); };

    std::printf("Cost of a voice, stereo at 48 kHz, blocks of 256\n\n");
    std::printf("| Instrument | Idle: ns/sample | 1 voice | 8 voices | 16 voices | Per voice: ns/sample | Voices per core |\n");
    std::printf("|------------|----------------:|--------:|---------:|----------:|---------------------:|----------------:|\n");
    for (const auto& test : cases)
    {
        if (!wanted(test))
            continue;
        const double idle = secondsHolding(test, 0);
        const double one = secondsHolding(test, 1);
        const double eight = secondsHolding(test, 8);
        const double many = secondsHolding(test, voicesPerTrack);
        const double perVoice = (many - idle) / voicesPerTrack;
        std::printf("| %s | %.1f | %.1f | %.1f | %.1f | %.1f | %.0f |\n", test.name, idle * 1e9,
                    one * 1e9, eight * 1e9, many * 1e9, perVoice * 1e9, 1.0 / (perVoice * sampleRate));
        std::fflush(stdout);
    }

    std::printf("\nTracks of %d held voices each, played by the engine\n\n", voicesPerTrack);
    std::printf("| Instrument | Block | Budget | Voices within 100%% | Median block there | Within 50%% |\n");
    std::printf("|------------|------:|-------:|-------------------:|-------------------:|-----------:|\n");
    // Every block size for the synth's default sound; 256 for the others.
    for (const auto& test : cases)
    {
        if (!wanted(test))
            continue;
        const bool first = &test == &cases.front();
        for (const int block : first ? std::vector<int>{128, 256, 512} : std::vector<int>{256})
        {
            const auto full = mostTracksWithin(test, 1.0, block);
            const auto half = mostTracksWithin(test, 0.5, block);
            std::printf("| %s | %d | %.2f ms | %d | %.2f ms | %d |\n", test.name, block,
                        1000.0 * block / sampleRate, full.tracks * voicesPerTrack, full.median,
                        half.tracks * voicesPerTrack);
            std::fflush(stdout);
        }
    }
    return 0;
}
