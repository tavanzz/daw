#include "daw/engine/io/DiskStreamer.h"

#include <algorithm>
#include <set>

namespace daw::engine::io
{

namespace
{

std::unique_ptr<juce::AudioFormatReader> openFormatReader(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    formats.registerFormat(new juce::AiffAudioFormat(), false);
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
}

std::size_t bytesOf(int numChannels)
{
    return static_cast<std::size_t>(numChannels) * DiskStreamer::blockLength * sizeof(float);
}

} // namespace

// StreamingAudioSource ---------------------------------------------------------------

StreamingAudioSource::StreamingAudioSource(DiskStreamer& owner, juce::File source, int channels,
                                           double sourceRate, juce::int64 sourceLength,
                                           juce::int64 streamLength)
    : streamer(owner)
    , file(std::move(source))
    , numChannels(channels)
    , fileSampleRate(sourceRate)
    , fileLength(sourceLength)
    , length(streamLength)
{
    const auto numBlocks =
        static_cast<std::size_t>((length + DiskStreamer::blockLength - 1) / DiskStreamer::blockLength);
    published = std::make_unique<std::atomic<const StreamBlock*>[]>(numBlocks);
    owned.resize(numBlocks);
}

StreamingAudioSource::~StreamingAudioSource()
{
    streamer.forget(*this);
}

double StreamingAudioSource::getSampleRate() const noexcept
{
    return streamer.getSampleRate();
}

bool StreamingAudioSource::read(int channel, juce::int64 start, float* destination,
                                int numSamples) const noexcept
{
    jassert(channel >= 0 && channel < numChannels);

    const juce::int64 end = start + numSamples;
    const juce::int64 validStart = std::clamp(start, juce::int64{0}, length);
    const juce::int64 validEnd = std::clamp(end, validStart, length);

    // Silence before and after the file.
    juce::FloatVectorOperations::clear(destination, static_cast<int>(validStart - start));
    juce::FloatVectorOperations::clear(destination + (validEnd - start),
                                       static_cast<int>(end - validEnd));

    bool complete = true;
    for (juce::int64 position = validStart; position < validEnd;)
    {
        const auto block = static_cast<std::size_t>(position / DiskStreamer::blockLength);
        const auto offset = static_cast<int>(position % DiskStreamer::blockLength);
        const auto count =
            static_cast<int>(std::min<juce::int64>(DiskStreamer::blockLength - offset, validEnd - position));

        // Sequentially consistent, like the RenderClock: a block retired after this
        // load is not reused before this audio block ends.
        const StreamBlock* loaded = published[block].load();
        if (loaded == nullptr && streamer.getMode() == DiskStreamer::Mode::synchronous)
        {
            streamer.loadSynchronously(*this, block);
            loaded = published[block].load();
        }

        float* into = destination + (position - start);
        if (loaded != nullptr)
            juce::FloatVectorOperations::copy(into, loaded->samples.getReadPointer(channel, offset),
                                              count);
        else
            juce::FloatVectorOperations::clear(into, count);

        complete = complete && loaded != nullptr;
        position += count;
    }

    if (!complete)
        streamer.dropouts.fetch_add(1, std::memory_order_relaxed);
    return complete;
}

bool StreamingAudioSource::isReady(juce::int64 start, juce::int64 numSamples) const noexcept
{
    const juce::int64 from = std::clamp(start, juce::int64{0}, length);
    const juce::int64 to = std::clamp(start + numSamples, from, length);
    if (from >= to)
        return true;

    for (auto block = from / DiskStreamer::blockLength; block <= (to - 1) / DiskStreamer::blockLength;
         ++block)
        if (published[static_cast<std::size_t>(block)].load() == nullptr)
            return false;

    return true;
}

// DiskStreamer -----------------------------------------------------------------------

class DiskStreamer::Thread final : public juce::Thread
{
public:
    explicit Thread(DiskStreamer& streamerToRun)
        : juce::Thread("Disk read-ahead")
        , streamer(streamerToRun)
    {
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            streamer.service();
            wait(5);
        }
    }

private:
    DiskStreamer& streamer;
};

DiskStreamer::DiskStreamer(Settings settingsToUse, const RenderClock* renderClock)
    : settings(std::move(settingsToUse))
    , clock(renderClock)
{
    jassert(settings.sampleRate > 0.0);
    if (!settings.openReader)
        settings.openReader = openFormatReader;

    if (settings.mode == Mode::readAhead)
    {
        thread = std::make_unique<Thread>(*this);
        thread->startThread();
    }
}

DiskStreamer::~DiskStreamer()
{
    if (thread != nullptr)
        thread->stopThread(-1);

    // The plan may hold the last references to sources, whose destructors come back
    // here: let them go while everything is still in place.
    std::shared_ptr<const Plan> lastPlan;
    {
        const std::scoped_lock lock(planLock);
        lastPlan = std::move(plan);
        playhead = nullptr;
    }
    lastPlan.reset();

    // Any other source still alive refers to this streamer: they must be gone.
    jassert(sources.empty());
}

std::shared_ptr<StreamingAudioSource> DiskStreamer::open(const juce::File& file, juce::String& error)
{
    const auto quotedName = "\"" + file.getFileName() + "\"";
    if (!file.existsAsFile())
    {
        error = quotedName + " does not exist.";
        return nullptr;
    }

    const auto reader = settings.openReader(file);
    if (reader == nullptr)
    {
        error = quotedName + " is not a WAV or AIFF file, or it is damaged.";
        return nullptr;
    }

    if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
    {
        error = quotedName + " has an invalid audio format.";
        return nullptr;
    }

    // JUCE reports a WAV whose data chunk was cut short as having no samples.
    if (reader->lengthInSamples <= 0)
    {
        error = quotedName + " contains no audio, or is incomplete.";
        return nullptr;
    }

    const auto streamLength =
        juce::exactlyEqual(reader->sampleRate, settings.sampleRate)
            ? reader->lengthInSamples
            : dsp::resampledLength(reader->lengthInSamples, reader->sampleRate, settings.sampleRate);

    std::shared_ptr<StreamingAudioSource> source(new StreamingAudioSource(
        *this, file, static_cast<int>(reader->numChannels), reader->sampleRate,
        reader->lengthInSamples, streamLength));

    const std::scoped_lock lock(loadLock);
    sources.push_back(source.get());
    return source;
}

void DiskStreamer::setPlan(const Arrangement& arrangement)
{
    auto newPlan = std::make_shared<Plan>();
    for (const auto& shared : arrangement.sources)
    {
        auto streamed = std::const_pointer_cast<StreamingAudioSource>(
            std::dynamic_pointer_cast<const StreamingAudioSource>(shared));
        if (streamed != nullptr && &streamed->streamer == this)
            newPlan->sources.push_back(std::move(streamed));
    }

    for (const auto& track : arrangement.tracks)
        for (const auto& segment : track.segments)
            if (std::any_of(newPlan->sources.begin(), newPlan->sources.end(),
                            [&](const auto& source) { return source.get() == segment.source; }))
                newPlan->segments.push_back(segment);

    const std::scoped_lock lock(planLock);
    plan = std::move(newPlan);
}

void DiskStreamer::followPlayhead(std::function<Playhead()> newPlayhead)
{
    const std::scoped_lock lock(planLock);
    playhead = std::move(newPlayhead);
}

void DiskStreamer::service()
{
    std::shared_ptr<const Plan> currentPlan;
    std::function<Playhead()> currentPlayhead;
    {
        const std::scoped_lock lock(planLock);
        currentPlan = plan;
        currentPlayhead = playhead;
    }

    const std::scoped_lock loading(loadingLock);
    {
        const std::scoped_lock lock(loadLock);
        ++round;
    }

    if (currentPlan != nullptr && currentPlayhead)
    {
        const auto wanted = wantedBlocks(*currentPlan, currentPlayhead());

        {
            // What is wanted now must not be dropped to make room for the rest.
            const std::scoped_lock lock(loadLock);
            for (const auto& item : wanted)
                if (auto& block = item.source->owned[item.block])
                    block->lastWanted = round;
        }

        for (const auto& item : wanted)
            if (!load(*item.source, item.block))
                break; // no room: the memory budget is full of wanted blocks
    }

    const std::scoped_lock lock(loadLock);
    recycleRetired();
}

void DiskStreamer::prefetch(juce::int64 start, juce::int64 length)
{
    std::shared_ptr<const Plan> currentPlan;
    {
        const std::scoped_lock lock(planLock);
        currentPlan = plan;
    }
    if (currentPlan == nullptr)
        return;

    std::vector<Wanted> wanted;
    addWanted(*currentPlan, start, start + length, wanted);

    const std::scoped_lock loading(loadingLock);
    {
        const std::scoped_lock lock(loadLock);
        ++round;
        for (const auto& item : wanted)
            if (auto& block = item.source->owned[item.block])
                block->lastWanted = round;
    }

    for (const auto& item : wanted)
        if (!load(*item.source, item.block))
            break;
}

void DiskStreamer::loadSynchronously(const StreamingAudioSource& source, std::size_t block)
{
    // Offline: the reading thread is the loading thread, and waits.
    const std::scoped_lock loading(loadingLock);
    {
        const std::scoped_lock lock(loadLock);
        ++round;
    }
    load(const_cast<StreamingAudioSource&>(source), block);
}

std::vector<DiskStreamer::Wanted> DiskStreamer::wantedBlocks(const Plan& currentPlan,
                                                             const Playhead& head) const
{
    std::vector<Wanted> wanted;
    const auto ahead = static_cast<juce::int64>(settings.lookaheadSeconds * settings.sampleRate);
    const auto second = static_cast<juce::int64>(settings.sampleRate);
    const bool loops = head.looping && head.loopEnd > head.loopStart;

    if (head.playing)
    {
        // Up to the loop end and on from the loop start, or straight on.
        if (loops && head.position < head.loopEnd && head.position + ahead > head.loopEnd)
        {
            addWanted(currentPlan, head.position, head.loopEnd, wanted);
            addWanted(currentPlan, head.loopStart,
                      head.loopStart + (head.position + ahead - head.loopEnd), wanted);
        }
        else
        {
            addWanted(currentPlan, head.position, head.position + ahead, wanted);
        }
    }
    else
    {
        // Ready to start where playback would start.
        addWanted(currentPlan, head.position, head.position + second, wanted);
    }

    if (loops)
        addWanted(currentPlan, head.loopStart, head.loopStart + second, wanted);

    return wanted;
}

void DiskStreamer::addWanted(const Plan& currentPlan, juce::int64 start, juce::int64 end,
                             std::vector<Wanted>& wanted) const
{
    for (const auto& segment : currentPlan.segments)
    {
        const auto from = std::max(segment.start, start);
        const auto to = std::min(segment.end, end);
        if (from >= to)
            continue;

        const auto source = std::find_if(currentPlan.sources.begin(), currentPlan.sources.end(),
                                         [&](const auto& candidate) {
                                             return candidate.get() == segment.source;
                                         });
        const auto sourceFrom = std::max(juce::int64{0}, segment.sourceStart + (from - segment.start));
        const auto sourceTo = std::min((*source)->getLength(), segment.sourceStart + (to - segment.start));
        if (sourceFrom >= sourceTo)
            continue;

        for (auto block = sourceFrom / blockLength; block <= (sourceTo - 1) / blockLength; ++block)
        {
            const auto index = static_cast<std::size_t>(block);
            const bool known = std::any_of(wanted.begin(), wanted.end(), [&](const Wanted& item) {
                return item.source == *source && item.block == index;
            });
            if (!known)
                wanted.push_back({*source, index});
        }
    }
}

bool DiskStreamer::load(StreamingAudioSource& source, std::size_t block)
{
    std::unique_ptr<StreamBlock> fresh;
    juce::AudioFormatReader* reader = nullptr;
    {
        const std::scoped_lock lock(loadLock);
        if (source.owned[block] != nullptr)
            return true;

        fresh = takeBlock(source.numChannels);
        if (fresh == nullptr)
            return false;
        reader = readerFor(source);
    }

    // The disk, without holding the lock: a slow file stalls only this thread.
    const bool read = reader != nullptr && readBlock(source, *reader, block, *fresh);

    const std::scoped_lock lock(loadLock);
    if (!read)
    {
        freeBlocks.push_back(std::move(fresh));
        return true; // an unreadable block plays as a dropout; carry on with the others
    }

    fresh->lastWanted = round;
    source.published[block].store(fresh.get(), std::memory_order_release);
    source.owned[block] = std::move(fresh);
    return true;
}

std::unique_ptr<StreamBlock> DiskStreamer::takeBlock(int numChannels)
{
    recycleRetired();

    const auto matching = std::find_if(freeBlocks.begin(), freeBlocks.end(), [&](const auto& block) {
        return block->samples.getNumChannels() == numChannels;
    });
    if (matching != freeBlocks.end())
    {
        auto block = std::move(*matching);
        freeBlocks.erase(matching);
        return block;
    }

    // Free blocks of another shape make room for one of this shape.
    while (!freeBlocks.empty() && memoryInUse.load() + bytesOf(numChannels) > settings.memoryBudget)
    {
        memoryInUse -= bytesOf(freeBlocks.back()->samples.getNumChannels());
        freeBlocks.pop_back();
    }

    if (memoryInUse.load() + bytesOf(numChannels) <= settings.memoryBudget)
    {
        auto block = std::make_unique<StreamBlock>();
        block->samples.setSize(numChannels, blockLength);
        memoryInUse += bytesOf(numChannels);
        return block;
    }

    // Full: drop the block needed least recently (never one wanted this round). The
    // audio thread may be reading it, so it is retired, not reused at once.
    StreamingAudioSource* oldestSource = nullptr;
    std::size_t oldestBlock = 0;
    std::uint64_t oldest = round;
    for (auto* candidate : sources)
        for (std::size_t index = 0; index < candidate->owned.size(); ++index)
            if (const auto& block = candidate->owned[index]; block != nullptr && block->lastWanted < oldest)
            {
                oldest = block->lastWanted;
                oldestSource = candidate;
                oldestBlock = index;
            }

    if (oldestSource == nullptr)
        return nullptr;

    oldestSource->published[oldestBlock].store(nullptr);
    retired.push_back({std::move(oldestSource->owned[oldestBlock]),
                       clock != nullptr ? clock->ticket() : 0});
    recycleRetired();

    // Still in use by an audio block in progress: try again next round.
    return freeBlocks.empty() ? nullptr : takeBlock(numChannels);
}

void DiskStreamer::recycleRetired()
{
    for (auto item = retired.begin(); item != retired.end();)
    {
        if (clock == nullptr || clock->isPast(item->ticket))
        {
            freeBlocks.push_back(std::move(item->block));
            item = retired.erase(item);
        }
        else
        {
            ++item;
        }
    }
}

juce::AudioFormatReader* DiskStreamer::readerFor(const StreamingAudioSource& source)
{
    auto& reader = readers[&source];
    if (reader == nullptr)
        reader = settings.openReader(source.file);
    return reader.get();
}

const dsp::Resampling& DiskStreamer::resamplingFor(double fileRate)
{
    const std::scoped_lock lock(loadLock);
    auto& resampling = resamplings[fileRate];
    if (resampling == nullptr)
        resampling = std::make_unique<dsp::Resampling>(fileRate, settings.sampleRate);
    return *resampling;
}

bool DiskStreamer::readBlock(const StreamingAudioSource& source, juce::AudioFormatReader& reader,
                             std::size_t block, StreamBlock& into)
{
    return readStream(source, reader, static_cast<juce::int64>(block) * blockLength, into.samples,
                      fileScratch);
}

bool DiskStreamer::readStream(const StreamingAudioSource& source, juce::AudioFormatReader& reader,
                              juce::int64 start, juce::AudioBuffer<float>& into,
                              juce::AudioBuffer<float>& scratch)
{
    const auto count = static_cast<int>(std::min<juce::int64>(into.getNumSamples(), source.length - start));
    into.clear();
    if (count <= 0)
        return true;

    if (juce::exactlyEqual(source.fileSampleRate, settings.sampleRate))
        return reader.read(into.getArrayOfWritePointers(), into.getNumChannels(), start, count);

    // Converted as dsp::resample() converts the whole file, a block at a time.
    const auto& resampling = resamplingFor(source.fileSampleRate);
    const auto first = std::max(juce::int64{0}, resampling.firstInputFor(start));
    const auto end = std::min(source.fileLength, resampling.endInputFor(start, count));
    const auto inputCount = static_cast<int>(std::max(juce::int64{0}, end - first));

    scratch.setSize(into.getNumChannels(), std::max(1, inputCount), false, false, true);
    if (inputCount > 0 &&
        !reader.read(scratch.getArrayOfWritePointers(), scratch.getNumChannels(), first, inputCount))
        return false;

    resampling.resampleRange(scratch.getArrayOfReadPointers(), into.getNumChannels(), first,
                             inputCount, source.fileLength, start, count,
                             into.getArrayOfWritePointers());
    return true;
}

bool DiskStreamer::scan(const StreamingAudioSource& source,
                        const std::function<void(const juce::AudioBuffer<float>&, int)>& consume)
{
    const auto reader = settings.openReader(source.file);
    if (reader == nullptr)
        return false;

    juce::AudioBuffer<float> block(source.numChannels, blockLength);
    juce::AudioBuffer<float> scratch;
    for (juce::int64 start = 0; start < source.length; start += blockLength)
    {
        if (!readStream(source, *reader, start, block, scratch))
            return false;
        consume(block, static_cast<int>(std::min<juce::int64>(blockLength, source.length - start)));
    }
    return true;
}

void DiskStreamer::forget(StreamingAudioSource& source)
{
    // Nothing reads a source being destroyed: its blocks can be reused at once.
    const std::scoped_lock lock(loadLock);
    std::erase(sources, &source);
    readers.erase(&source);
    for (auto& block : source.owned)
        if (block != nullptr)
            freeBlocks.push_back(std::move(block));
}

} // namespace daw::engine::io
