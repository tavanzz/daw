#include "daw/engine/io/SourcePool.h"

namespace daw::engine::io
{

namespace
{

PooledSource loadSource(const juce::File& file, DiskStreamer& streamer, bool computePeaks)
{
    PooledSource source;
    auto streamed = streamer.open(file, source.error);
    if (streamed == nullptr)
    {
        source.status = PooledSource::Status::failed;
        return source;
    }

    source.formatName = streamed->getFormatName();
    source.fileSampleRate = streamed->getFileSampleRate();
    source.bitsPerSample = streamed->getFileBitsPerSample();
    source.numChannels = streamed->getNumChannels();

    if (computePeaks)
    {
        dsp::WaveformPeaks::Builder peaks(streamed->getNumChannels());
        const bool scanned = streamer.scan(*streamed, [&](const juce::AudioBuffer<float>& block,
                                                          int count) {
            peaks.append(block.getArrayOfReadPointers(), count);
        });
        if (!scanned)
        {
            source.status = PooledSource::Status::failed;
            source.error = "\"" + file.getFileName() + "\" could not be read completely.";
            return source;
        }
        source.peaks = std::make_shared<const dsp::WaveformPeaks>(peaks.build());
    }

    source.audio = std::move(streamed);
    source.status = PooledSource::Status::ready;
    return source;
}

} // namespace

SourcePool::SourcePool(double newSampleRate, int numLoaderThreads, StreamingOptions options)
    : sampleRate(newSampleRate)
    , streaming(options)
    , loaders(juce::ThreadPoolOptions{}
                  .withThreadName("Audio file loader")
                  .withNumberOfThreads(numLoaderThreads))
{
    jassert(newSampleRate > 0.0);
    streamer = makeStreamer();
}

SourcePool::~SourcePool()
{
    loaders.removeAllJobs(true, -1);
}

std::shared_ptr<DiskStreamer> SourcePool::makeStreamer() const
{
    DiskStreamer::Settings settings;
    settings.sampleRate = sampleRate;
    settings.mode = streaming.mode;
    settings.memoryBudget = streaming.memoryBudget;
    auto newStreamer = std::make_shared<DiskStreamer>(settings, streaming.clock);
    if (playhead)
        newStreamer->followPlayhead(playhead);
    return newStreamer;
}

void SourcePool::followPlayhead(std::function<DiskStreamer::Playhead()> newPlayhead)
{
    playhead = std::move(newPlayhead);
    streamer->followPlayhead(playhead);
}

void SourcePool::setSampleRate(double newSampleRate)
{
    jassert(newSampleRate > 0.0);
    if (juce::exactlyEqual(newSampleRate, sampleRate))
        return;

    sampleRate = newSampleRate;
    streamer = makeStreamer();

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

    loaders.addJob([this, file, path, generation, loadingStreamer = streamer,
                    peaks = streaming.computePeaks] {
        Finished result{path, generation, loadSource(file, *loadingStreamer, peaks)};
        {
            const std::scoped_lock lock(finishedLock);
            finished.push_back(std::move(result));
        }
        --runningLoads;
    });
}

} // namespace daw::engine::io
