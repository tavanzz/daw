#pragma once

#include "daw/engine/Sample.h"
#include "daw/model/Project.h"

#include <juce_core/juce_core.h>

#include <map>
#include <memory>
#include <mutex>

namespace daw::controller
{

/**
    The samples instruments play (a sampler's, a drum sampler's pads), each file
    loaded once, whole, at the device's sample rate, with its band-limited levels
    (engine::Sample), on worker threads. A new rate loads them again.

    Arrangements share the samples they play, so dropping one here never frees what
    the engine still reads.

    Message thread, except the workers.
*/
class SampleCache
{
public:
    enum class Status
    {
        none,
        loading,
        ready,
        failed
    };

    explicit SampleCache(double sampleRate);
    ~SampleCache();

    SampleCache(const SampleCache&) = delete;
    SampleCache& operator=(const SampleCache&) = delete;

    /** Reloads every sample at the new rate. */
    void setSampleRate(double sampleRate);
    [[nodiscard]] double getSampleRate() const noexcept { return rate; }

    /** Starts loading the file, unless it is loaded or loading. */
    void request(const juce::File& file);

    /** Starts loading every file the project's instruments play. */
    void requestAll(const model::Project& project);

    /** The loaded sample; null while loading, failed or never asked for. */
    [[nodiscard]] std::shared_ptr<const engine::Sample> find(const juce::File& file) const;
    [[nodiscard]] Status getStatus(const juce::File& file) const;
    [[nodiscard]] juce::String getError(const juce::File& file) const;

    /** Takes in what finished loading; true if anything did. */
    bool collectFinished();

    /** Waits until nothing loads (or the timeout passes), then collectFinished(). */
    bool waitForLoads(int timeoutMilliseconds);

    /** Forgets every sample (another project). */
    void clear();

private:
    struct Entry
    {
        Status status = Status::loading;
        std::shared_ptr<const engine::Sample> sample;
        juce::String error;
        int generation = 0;
    };

    struct Finished
    {
        juce::String path;
        int generation = 0;
        std::shared_ptr<const engine::Sample> sample;
        juce::String error;
    };

    void start(const juce::File& file);

    double rate;
    int generation = 0;
    std::map<juce::String, Entry> entries;
    juce::ThreadPool workers{2};

    std::mutex finishedLock;
    std::vector<Finished> finished;
    std::atomic<int> pending{0};
};

} // namespace daw::controller
