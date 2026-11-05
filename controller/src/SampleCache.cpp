#include "daw/controller/SampleCache.h"

#include "daw/engine/io/AudioFileLoader.h"

namespace daw::controller
{

SampleCache::SampleCache(double sampleRate)
    : rate(sampleRate)
{
}

SampleCache::~SampleCache()
{
    workers.removeAllJobs(true, 30000);
}

void SampleCache::setSampleRate(double sampleRate)
{
    if (juce::exactlyEqual(sampleRate, rate))
        return;
    rate = sampleRate;
    ++generation;
    std::vector<juce::String> paths;
    for (const auto& [path, entry] : entries)
        paths.push_back(path);
    entries.clear();
    for (const auto& path : paths)
        start(juce::File(path));
}

void SampleCache::request(const juce::File& file)
{
    if (file == juce::File() || entries.contains(file.getFullPathName()))
        return;
    start(file);
}

void SampleCache::requestAll(const model::Project& project)
{
    for (int t = 0; t < project.getNumTracks(); ++t)
        if (const auto instrument = project.getTrack(t).getInstrument())
            for (const auto& [key, file] : instrument->getFiles())
                request(file);
}

void SampleCache::start(const juce::File& file)
{
    const auto path = file.getFullPathName();
    auto& entry = entries[path];
    entry = Entry{};
    entry.generation = generation;

    ++pending;
    workers.addJob([this, path, file, sampleRate = rate, jobGeneration = generation] {
        Finished result{path, jobGeneration, nullptr, {}};
        auto loaded = engine::io::loadAudioFile(file, sampleRate);
        if (loaded.succeeded())
            result.sample = engine::Sample::fromAudio(std::move(loaded.audio->samples), sampleRate);
        else
            result.error = loaded.error;
        {
            const std::lock_guard lock(finishedLock);
            finished.push_back(std::move(result));
        }
        --pending;
    });
}

std::shared_ptr<const engine::Sample> SampleCache::find(const juce::File& file) const
{
    const auto found = entries.find(file.getFullPathName());
    return found != entries.end() ? found->second.sample : nullptr;
}

SampleCache::Status SampleCache::getStatus(const juce::File& file) const
{
    const auto found = entries.find(file.getFullPathName());
    return found != entries.end() ? found->second.status : Status::none;
}

juce::String SampleCache::getError(const juce::File& file) const
{
    const auto found = entries.find(file.getFullPathName());
    return found != entries.end() ? found->second.error : juce::String();
}

bool SampleCache::collectFinished()
{
    std::vector<Finished> done;
    {
        const std::lock_guard lock(finishedLock);
        done.swap(finished);
    }

    bool any = false;
    for (auto& result : done)
    {
        const auto found = entries.find(result.path);
        if (found == entries.end() || found->second.generation != result.generation ||
            result.generation != generation)
            continue; // asked for again since, or at another rate
        auto& entry = found->second;
        entry.sample = std::move(result.sample);
        entry.error = result.error;
        entry.status = entry.sample != nullptr ? Status::ready : Status::failed;
        any = true;
    }
    return any;
}

bool SampleCache::waitForLoads(int timeoutMilliseconds)
{
    const auto deadline = juce::Time::getMillisecondCounter() + static_cast<juce::uint32>(timeoutMilliseconds);
    while (pending.load() > 0 && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(1);
    collectFinished();
    return pending.load() == 0;
}

void SampleCache::clear()
{
    entries.clear();
    ++generation;
}

} // namespace daw::controller
