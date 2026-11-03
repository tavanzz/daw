#pragma once

#include "daw/engine/AudioSource.h"
#include "daw/engine/dsp/WaveformPeaks.h"
#include "daw/engine/io/DiskStreamer.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace daw::engine::io
{

/** What the pool knows about one audio file. */
struct PooledSource
{
    enum class Status
    {
        loading,
        ready,
        failed
    };

    Status status = Status::loading;

    /** The decoded audio at the pool's sample rate, and its waveform; null unless ready. */
    std::shared_ptr<const AudioSource> audio;
    std::shared_ptr<const dsp::WaveformPeaks> peaks;

    juce::String error; ///< why loading failed, written for the user

    // The file as stored on disk (known once it could be opened).
    juce::String formatName;
    double fileSampleRate = 0.0;
    int bitsPerSample = 0;
    int numChannels = 0;
};

/** How a pool's files are streamed (see DiskStreamer). */
struct StreamingOptions
{
    /** readAhead for playback (with the engine's clock), synchronous for offline
        rendering, manual for tests that drive the read-ahead themselves. */
    DiskStreamer::Mode mode = DiskStreamer::Mode::synchronous;
    const RenderClock* clock = nullptr;
    std::size_t memoryBudget = std::size_t{512} << 20;

    /** Whether loading reads each file through for its waveform (an export needs none). */
    bool computePeaks = true;
};

/**
    Opens audio files in the background, each one once, however many clips use it.

    Files play from disk through the pool's DiskStreamer, converted to the pool's
    sample rate (the device's, or an export's); none is held whole in memory. Opening
    a file reads it through once, on a worker thread, for its waveform. Results wait
    until the owner calls collectFinishedLoads() on its own thread, which keeps every
    change to the pool on that thread and makes the pool easy to test. Sources stay
    open until clear(), so undoing the deletion of a clip never opens its file again.

    Message thread (or the one thread that owns the pool) only, except where noted.
*/
class SourcePool
{
public:
    explicit SourcePool(double sampleRate, int numLoaderThreads = 2,
                        StreamingOptions streaming = {});

    /** Waits for running loads to finish. */
    ~SourcePool();

    SourcePool(const SourcePool&) = delete;
    SourcePool& operator=(const SourcePool&) = delete;

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }

    /** Converts every file again for a new sample rate (the old audio is dropped:
        played at the wrong rate it would sound at the wrong pitch). */
    void setSampleRate(double newSampleRate);

    /** The streamer playing the pool's files: its plan, dropouts, memory. It changes
        with the sample rate. */
    [[nodiscard]] DiskStreamer& getStreamer() noexcept { return *streamer; }

    /** Where the read-ahead looks, now and for the streamers of later rates. */
    void followPlayhead(std::function<DiskStreamer::Playhead()> playhead);

    /** Starts loading the file unless it is loaded, loading or has failed. */
    void request(const juce::File& file);

    /** Loads a file again even if it is loaded or has failed (e.g. after it was
        replaced on disk). */
    void reload(const juce::File& file);

    /** Null if the file was never requested. */
    [[nodiscard]] const PooledSource* find(const juce::File& file) const;

    /** Applies the loads that have finished since the last call, then calls
        onSourceChanged for each file whose state changed. Returns how many. */
    int collectFinishedLoads();

    /** Called by collectFinishedLoads() for every file that finished loading. */
    std::function<void(const juce::File&)> onSourceChanged;

    /** Blocks until no load is running, or the timeout (in ms) passes; true if idle.
        For tests and offline rendering. */
    bool waitForLoads(int timeoutMilliseconds);

    /** Forgets every file. Loads still running are discarded when they finish. */
    void clear();

    /** How many loads have been started since the pool was made. */
    [[nodiscard]] int getNumLoadsStarted() const noexcept { return loadsStarted; }

private:
    struct Finished
    {
        juce::String path;
        int generation = 0;
        PooledSource source;
    };

    void startLoad(const juce::File& file);
    std::shared_ptr<DiskStreamer> makeStreamer() const;

    double sampleRate;
    StreamingOptions streaming;
    std::function<DiskStreamer::Playhead()> playhead;

    // Shared with the loads in progress, which may outlive a change of rate.
    std::shared_ptr<DiskStreamer> streamer;
    std::map<juce::String, PooledSource> sources; // by full path
    std::map<juce::String, int> loadGenerations;  // the load whose result is wanted
    int nextGeneration = 1;
    int loadsStarted = 0;

    // Worker threads -> owner thread.
    std::mutex finishedLock;
    std::vector<Finished> finished;
    std::atomic<int> runningLoads{0};

    juce::ThreadPool loaders;
};

} // namespace daw::engine::io
