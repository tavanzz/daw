#include "daw/engine/io/SourcePool.h"

#include "daw/engine/io/AudioFileLoader.h"

namespace daw::engine::io
{

namespace
{

PooledSource loadSource(const juce::File& file, double sampleRate)
{
    auto loaded = loadAudioFile(file, sampleRate);

    PooledSource source;
    source.error = loaded.error;
    source.formatName = loaded.formatName;
    source.fileSampleRate = loaded.sourceSampleRate;
    source.bitsPerSample = loaded.sourceBitsPerSample;
    source.numChannels = loaded.numChannels;

    if (!loaded.succeeded())
    {
        source.status = PooledSource::Status::failed;
        return source;
    }

    source.peaks = std::make_shared<const dsp::WaveformPeaks>(
        dsp::WaveformPeaks::compute(loaded.audio->samples));
    source.audio = std::make_shared<const MemoryAudioSource>(std::move(*loaded.audio));
    source.status = PooledSource::Status::ready;
    return source;
}

} // namespace

SourcePool::SourcePool(double newSampleRate, int numLoaderThreads)
    : sampleRate(newSampleRate)
    , loaders(juce::ThreadPoolOptions{}
                  .withThreadName("Audio file loader")
                  .withNumberOfThreads(numLoaderThreads))
{
    jassert(newSampleRate > 0.0);
}

SourcePool::~SourcePool()
{
    loaders.removeAllJobs(true, -1);
}

void SourcePool::setSampleRate(double newSampleRate)
{
    jassert(newSampleRate > 0.0);
    if (juce::exactlyEqual(newSampleRate, sampleRate))
        return;

    sampleRate = newSampleRate;

    for (auto& [path, source] : sources)
    {
        source = PooledSource{};
        startLoad(juce::File(path));
    }
}

void SourcePool::request(const juce::File& file)
{
    if (!sources.contains(file.getFullPathName()))
        reload(file);
}

void SourcePool::reload(const juce::File& file)
{
    sources[file.getFullPathName()] = PooledSource{};
    startLoad(file);
}

const PooledSource* SourcePool::find(const juce::File& file) const
{
    const auto found = sources.find(file.getFullPathName());
    return found != sources.end() ? &found->second : nullptr;
}

int SourcePool::collectFinishedLoads()
{
    std::vector<Finished> done;
    {
        const std::scoped_lock lock(finishedLock);
        done.swap(finished);
    }

    std::vector<juce::String> changed;
    for (auto& result : done)
    {
        // Only the latest load of a file that is still wanted counts.
        const auto wanted = loadGenerations.find(result.path);
        if (wanted == loadGenerations.end() || wanted->second != result.generation)
            continue;

        loadGenerations.erase(wanted);
        if (const auto source = sources.find(result.path); source != sources.end())
        {
            source->second = std::move(result.source);
            changed.push_back(result.path);
        }
    }

    // Callbacks last: they may request more files.
    if (onSourceChanged)
        for (const auto& path : changed)
            onSourceChanged(juce::File(path));

    return static_cast<int>(changed.size());
}

bool SourcePool::waitForLoads(int timeoutMilliseconds)
{
    const auto deadline = juce::Time::getMillisecondCounter() +
                          static_cast<juce::uint32>(juce::jmax(0, timeoutMilliseconds));

    while (runningLoads.load() > 0)
    {
        if (juce::Time::getMillisecondCounter() >= deadline)
            return false;
        juce::Thread::sleep(1);
    }

    return true;
}

void SourcePool::clear()
{
    sources.clear();
    loadGenerations.clear();
}

void SourcePool::startLoad(const juce::File& file)
{
    const auto path = file.getFullPathName();
    const int generation = nextGeneration++;
    loadGenerations[path] = generation;
    ++loadsStarted;
    ++runningLoads;

    loaders.addJob([this, file, path, generation, rate = sampleRate] {
        Finished result{path, generation, loadSource(file, rate)};
        {
            const std::scoped_lock lock(finishedLock);
            finished.push_back(std::move(result));
        }
        --runningLoads;
    });
}

} // namespace daw::engine::io
