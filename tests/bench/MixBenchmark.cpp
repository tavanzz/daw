// How many tracks the engine mixes within the time of one audio callback.
//
// Build the release preset and run:  build/release/tests/daw_bench
// Not part of CTest: timings depend on the machine and on what else runs on it.
//
// Every track plays a stereo clip with its own volume and pan from one of 256
// different one-second sources (98 MB, more than the CPU caches hold), each track
// from its own point in its source, looping: every block reads, gains and sums
// every track, mostly from main memory rather than from cache. Engine::process() is timed per block on
// this (normal priority) thread; a track count fits when the 99th percentile of
// the block times stays within the share of the budget asked for.

#include "daw/engine/Engine.h"
#include "daw/engine/dsp/PanLaw.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <memory>
#include <vector>

namespace
{

using daw::engine::Arrangement;
using daw::engine::ArrangementTrack;
using daw::engine::Engine;
using daw::engine::MemoryAudioSource;

constexpr double sampleRate = 48000.0;
constexpr int sourceLength = 48000; // one second
constexpr int distinctSources = 256;
constexpr int warmUpBlocks = 100;
constexpr int measuredBlocks = 600;

std::vector<std::shared_ptr<const MemoryAudioSource>> makeSources()
{
    std::vector<std::shared_ptr<const MemoryAudioSource>> sources;
    sources.reserve(distinctSources);
    juce::Random random(1);

    for (int s = 0; s < distinctSources; ++s)
    {
        daw::engine::AudioData data;
        data.sampleRate = sampleRate;
        data.samples.setSize(2, sourceLength);
        for (int channel = 0; channel < 2; ++channel)
            for (int n = 0; n < sourceLength; ++n)
                data.samples.setSample(channel, n, random.nextFloat() * 0.02f - 0.01f);
        sources.push_back(std::make_shared<const MemoryAudioSource>(std::move(data)));
    }

    return sources;
}

using Sources = std::vector<std::shared_ptr<const MemoryAudioSource>>;

std::unique_ptr<Arrangement> makeArrangement(int numTracks, const Sources& sources,
                                             bool alternateGains)
{
    auto arrangement = std::make_unique<Arrangement>();
    arrangement->sampleRate = sampleRate;
    arrangement->length = sourceLength;
    arrangement->tracks.reserve(static_cast<std::size_t>(numTracks));

    for (int t = 0; t < numTracks; ++t)
    {
        const auto& source = sources[static_cast<std::size_t>(t % distinctSources)];
        const auto pan = daw::engine::dsp::panGains(static_cast<double>(t % 21) / 10.0 - 1.0);
        const float volume = alternateGains ? 0.5f : 0.8f;

        ArrangementTrack track;
        track.id = t + 1;
        track.leftGain = volume * pan.left;
        track.rightGain = volume * pan.right;

        // Tracks sharing a source read it at different places: [offset, end) then
        // [0, offset), so they do not hit the same cache lines.
        const auto round = static_cast<juce::int64>(t / distinctSources);
        const juce::int64 offset = (round * 7919) % sourceLength;
        track.segments.push_back({source.get(), 0, sourceLength - offset, offset, 0, 0});
        if (offset > 0)
            track.segments.push_back({source.get(), sourceLength - offset, sourceLength, 0, 0, 0});
        arrangement->tracks.push_back(std::move(track));
    }

    arrangement->sources.assign(sources.begin(), sources.end());
    return arrangement;
}

struct Timing
{
    double medianMs = 0.0;
    double p99Ms = 0.0;
    double maxMs = 0.0;
};

// movingFaders: a new set of gains every 4 blocks, so gain ramps run all the time.
Timing measure(int numTracks, int blockSize, bool movingFaders, const Sources& sources)
{
    Engine engine;
    engine.prepare(sampleRate, blockSize);
    engine.setArrangement(makeArrangement(numTracks, sources, false));
    engine.setLooping(true);
    engine.play();

    juce::AudioBuffer<float> block(2, blockSize);
    std::vector<double> times;
    times.reserve(measuredBlocks);

    for (int i = 0; i < warmUpBlocks + measuredBlocks; ++i)
    {
        if (movingFaders && i % 4 == 0)
        {
            engine.setArrangement(makeArrangement(numTracks, sources, (i / 4) % 2 == 1));
            engine.collectGarbage();
        }

        const auto start = std::chrono::steady_clock::now();
        engine.process(block);
        const auto end = std::chrono::steady_clock::now();

        if (i >= warmUpBlocks)
            times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
    }

    std::sort(times.begin(), times.end());
    Timing timing;
    timing.medianMs = times[times.size() / 2];
    timing.p99Ms = times[times.size() * 99 / 100];
    timing.maxMs = times.back();
    return timing;
}

// The most tracks whose p99 block time stays within budgetShare of the block's
// duration: doubling, then bisection.
int mostTracksWithin(double budgetShare, int blockSize, bool movingFaders, const Sources& sources)
{
    const double budgetMs = 1000.0 * blockSize / sampleRate * budgetShare;
    const auto fits = [&](int tracks) {
        return measure(tracks, blockSize, movingFaders, sources).p99Ms <= budgetMs;
    };

    int low = 0;
    int high = 16;
    while (high <= (1 << 16) && fits(high))
    {
        low = high;
        high *= 2;
    }

    while (high - low > std::max(1, low / 50)) // within 2 %
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

    const auto sources = makeSources();

    std::printf("Tracks mixed within one audio callback at 48 kHz (p99 of %d blocks)\n\n",
                measuredBlocks);
    std::printf("| Block | Budget | Faders | Within 100%% | Within 50%% "
                "| 100 tracks: median / p99 / max |\n");
    std::printf("|------:|-------:|--------|------------:|-----------:"
                "|-------------------------------|\n");

    for (const int blockSize : {64, 128, 256, 512})
    {
        for (const bool moving : {false, true})
        {
            const int full = mostTracksWithin(1.0, blockSize, moving, sources);
            const int half = mostTracksWithin(0.5, blockSize, moving, sources);
            const auto hundred = measure(100, blockSize, moving, sources);

            std::printf("| %5d | %4.2f ms | %-6s | %11d | %10d | %.3f / %.3f / %.3f ms |\n",
                        blockSize, 1000.0 * blockSize / sampleRate, moving ? "moving" : "still",
                        full, half, hundred.medianMs, hundred.p99Ms, hundred.maxMs);
            std::fflush(stdout);
        }
    }

    return 0;
}
