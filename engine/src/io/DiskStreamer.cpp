#include "daw/engine/io/DiskStreamer.h"

#include "daw/engine/dsp/Resampler.h"

#include <algorithm>
#include <map>
#include <mutex>

namespace daw::engine::io
{

namespace
{

constexpr int blockLength = DiskStreamer::blockLength;

std::unique_ptr<juce::AudioFormatReader> openFormatReader(const juce::File& file)
{
    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    formats.registerFormat(new juce::AiffAudioFormat(), false);
    return std::unique_ptr<juce::AudioFormatReader>(formats.createReaderFor(file));
}

std::size_t bytesOf(int numChannels)
{
    return static_cast<std::size_t>(numChannels) * blockLength * sizeof(float);
}

} // namespace

/** Everything a streamer knows, shared with its sources so that it outlives them. */
class DiskStreamerCore
{
public:
    DiskStreamerCore(DiskStreamer::Settings settingsToUse, const RenderClock* renderClock)
        : settings(std::move(settingsToUse))
        , clock(renderClock)
    {
        if (!settings.openReader)
            settings.openReader = openFormatReader;
    }

    std::shared_ptr<StreamingAudioSource> open(const std::shared_ptr<DiskStreamerCore>& self,
                                               const juce::File& file, juce::String& error);
    void setPlan(std::initializer_list<const Arrangement*> arrangements);
    void followPlayhead(std::function<DiskStreamer::Playhead()> newPlayhead);
    void service();
    void prefetch(juce::int64 start, juce::int64 length);
    void loadSynchronously(const StreamingAudioSource& source, std::size_t block);
    bool scan(const StreamingAudioSource& source,
              const std::function<void(const juce::AudioBuffer<float>&, int)>& consume);
    void forget(StreamingAudioSource& source);
    void dropPlan();
    bool hasSources();

    const DiskStreamer::Settings& getSettings() const noexcept { return settings; }

    std::atomic<std::uint64_t> dropouts{0};
    std::atomic<std::size_t> memoryInUse{0};

    // The streamer is gone: no more loading (a synchronous read finds silence).
    std::atomic<bool> stopped{false};

private:
    struct Wanted
    {
        std::shared_ptr<StreamingAudioSource> source;
        std::size_t block = 0;
    };

    struct Plan
    {
        std::vector<ClipSegment> segments;
        std::vector<std::shared_ptr<StreamingAudioSource>> sources;
    };

    struct Retired
    {
        std::unique_ptr<StreamBlock> block;
        std::uint64_t ticket = 0;
    };

    std::vector<Wanted> wantedBlocks(const Plan& plan, const DiskStreamer::Playhead& head) const;
    void addWanted(const Plan& plan, juce::int64 start, juce::int64 end,
                   std::vector<Wanted>& wanted) const;
    void markWanted(const std::vector<Wanted>& wanted);
    bool load(StreamingAudioSource& source, std::size_t block);
    std::unique_ptr<StreamBlock> takeBlock(int numChannels);
    void recycleRetired();
    juce::AudioFormatReader* readerFor(const StreamingAudioSource& source);
    const dsp::Resampling& resamplingFor(double fileRate);
    bool readStream(const StreamingAudioSource& source, juce::AudioFormatReader& reader,
                    juce::int64 start, juce::AudioBuffer<float>& into,
                    juce::AudioBuffer<float>& scratch);

    DiskStreamer::Settings settings;
    const RenderClock* clock;

    // Plan and playhead: set by the message thread, read by the loading side.
    std::mutex planLock;
    std::shared_ptr<const Plan> plan;
    std::function<DiskStreamer::Playhead()> playhead;

    // One loader at a time (the read-ahead thread, a prefetch, a synchronous read):
    // it uses the readers and fileScratch, and does the disk work.
    std::mutex loadingLock;
    juce::AudioBuffer<float> fileScratch;

    // Bookkeeping, held briefly and never during disk work.
    std::recursive_mutex lock;
    std::vector<StreamingAudioSource*> sources; // every live source, for eviction
    std::vector<std::unique_ptr<StreamBlock>> freeBlocks;
    std::vector<Retired> retired;
    std::uint64_t round = 0;
    std::map<const StreamingAudioSource*, std::unique_ptr<juce::AudioFormatReader>> readers;
    std::map<double, std::unique_ptr<dsp::Resampling>> resamplings;
};

// StreamingAudioSource ---------------------------------------------------------------

StreamingAudioSource::StreamingAudioSource(std::shared_ptr<DiskStreamerCore> owner,
                                           const FileInfo& info, juce::int64 streamLength)
    : core(std::move(owner))
    , file(info.file)
    , numChannels(info.numChannels)
    , fileSampleRate(info.sampleRate)
    , fileLength(info.length)
    , length(streamLength)
    , formatName(info.formatName)
    , fileBitsPerSample(info.bitsPerSample)
{
    const auto numBlocks = static_cast<std::size_t>((length + blockLength - 1) / blockLength);
    published = std::make_unique<std::atomic<const StreamBlock*>[]>(numBlocks);
    owned.resize(numBlocks);
}

StreamingAudioSource::~StreamingAudioSource()
{
    core->forget(*this);
}

double StreamingAudioSource::getSampleRate() const noexcept
{
    return core->getSettings().sampleRate;
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

    const bool synchronous = core->getSettings().mode == DiskStreamer::Mode::synchronous;
    bool complete = true;

    for (juce::int64 position = validStart; position < validEnd;)
    {
        const auto block = static_cast<std::size_t>(position / blockLength);
        const auto offset = static_cast<int>(position % blockLength);
        const auto count =
            static_cast<int>(std::min<juce::int64>(blockLength - offset, validEnd - position));

        // Sequentially consistent, like the RenderClock: a block retired after this
        // load is not reused before this audio block ends.
        const StreamBlock* loaded = published[block].load();
        if (loaded == nullptr && synchronous)
        {
            core->loadSynchronously(*this, block);
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
        core->dropouts.fetch_add(1, std::memory_order_relaxed);
    return complete;
}

bool StreamingAudioSource::isReady(juce::int64 start, juce::int64 numSamples) const noexcept
{
    const juce::int64 from = std::clamp(start, juce::int64{0}, length);
    const juce::int64 to = std::clamp(start + numSamples, from, length);
    if (from >= to)
        return true;

    for (auto block = from / blockLength; block <= (to - 1) / blockLength; ++block)
        if (published[static_cast<std::size_t>(block)].load() == nullptr)
            return false;

    return true;
}

// DiskStreamerCore -------------------------------------------------------------------

std::shared_ptr<StreamingAudioSource> DiskStreamerCore::open(
    const std::shared_ptr<DiskStreamerCore>& self, const juce::File& file, juce::String& error)
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

    const StreamingAudioSource::FileInfo info{file,
                                              static_cast<int>(reader->numChannels),
                                              reader->sampleRate,
                                              reader->lengthInSamples,
                                              reader->getFormatName(),
                                              static_cast<int>(reader->bitsPerSample)};
    const auto streamLength =
        juce::exactlyEqual(info.sampleRate, settings.sampleRate)
            ? info.length
            : dsp::resampledLength(info.length, info.sampleRate, settings.sampleRate);

    std::shared_ptr<StreamingAudioSource> source(new StreamingAudioSource(self, info, streamLength));

    const std::scoped_lock bookkeeping(lock);
    sources.push_back(source.get());
    return source;
}

void DiskStreamerCore::setPlan(std::initializer_list<const Arrangement*> arrangements)
{
    auto newPlan = std::make_shared<Plan>();

    for (const auto* arrangement : arrangements)
    {
        if (arrangement == nullptr)
            continue;

        for (const auto& shared : arrangement->sources)
        {
            auto streamed = std::const_pointer_cast<StreamingAudioSource>(
                std::dynamic_pointer_cast<const StreamingAudioSource>(shared));
            if (streamed != nullptr && streamed->core.get() == this &&
                std::find(newPlan->sources.begin(), newPlan->sources.end(), streamed) ==
                    newPlan->sources.end())
                newPlan->sources.push_back(std::move(streamed));
        }

        for (const auto& track : arrangement->tracks)
            for (const auto& segment : track.segments)
                if (std::any_of(newPlan->sources.begin(), newPlan->sources.end(),
                                [&](const auto& source) { return source.get() == segment.source; }))
                    newPlan->segments.push_back(segment);
    }

    const std::scoped_lock planning(planLock);
    plan = std::move(newPlan);
}

void DiskStreamerCore::followPlayhead(std::function<DiskStreamer::Playhead()> newPlayhead)
{
    const std::scoped_lock planning(planLock);
    playhead = std::move(newPlayhead);
}

void DiskStreamerCore::dropPlan()
{
    std::shared_ptr<const Plan> lastPlan;
    {
        const std::scoped_lock planning(planLock);
        lastPlan = std::move(plan);
        playhead = nullptr;
    }
    // Its sources may go now, outside the lock (their destructors come back here).
}

void DiskStreamerCore::service()
{
    std::shared_ptr<const Plan> currentPlan;
    std::function<DiskStreamer::Playhead()> currentPlayhead;
    {
        const std::scoped_lock planning(planLock);
        currentPlan = plan;
        currentPlayhead = playhead;
    }

    const std::scoped_lock loading(loadingLock);

    if (currentPlan != nullptr && currentPlayhead)
    {
        const auto wanted = wantedBlocks(*currentPlan, currentPlayhead());
        markWanted(wanted);

        for (const auto& item : wanted)
            if (!load(*item.source, item.block))
                break; // no room: the memory budget is full of wanted blocks
    }

    const std::scoped_lock bookkeeping(lock);
    recycleRetired();
}

void DiskStreamerCore::prefetch(juce::int64 start, juce::int64 length)
{
    std::shared_ptr<const Plan> currentPlan;
    {
        const std::scoped_lock planning(planLock);
        currentPlan = plan;
    }
    if (currentPlan == nullptr)
        return;

    std::vector<Wanted> wanted;
    addWanted(*currentPlan, start, start + length, wanted);

    const std::scoped_lock loading(loadingLock);
    markWanted(wanted);
    for (const auto& item : wanted)
        if (!load(*item.source, item.block))
            break;
}

void DiskStreamerCore::loadSynchronously(const StreamingAudioSource& source, std::size_t block)
{
    if (stopped.load())
        return;

    // Offline: the reading thread is the loading thread, and waits.
    const std::scoped_lock loading(loadingLock);
    {
        const std::scoped_lock bookkeeping(lock);
        ++round;
    }
    load(const_cast<StreamingAudioSource&>(source), block);
}

void DiskStreamerCore::markWanted(const std::vector<Wanted>& wanted)
{
    // A new round: what it wants must not be dropped to make room for the rest.
    const std::scoped_lock bookkeeping(lock);
    ++round;
    for (const auto& item : wanted)
        if (auto& block = item.source->owned[item.block])
            block->lastWanted = round;
}

std::vector<DiskStreamerCore::Wanted>
DiskStreamerCore::wantedBlocks(const Plan& currentPlan, const DiskStreamer::Playhead& head) const
{
    std::vector<Wanted> wanted;
    const auto ahead = static_cast<juce::int64>(settings.lookaheadSeconds * settings.sampleRate);
    const auto second = static_cast<juce::int64>(settings.sampleRate);
    const bool loops = head.looping && head.loopEnd > head.loopStart;

    if (head.cue >= 0)
        addWanted(currentPlan, head.cue, head.cue + second, wanted);

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

void DiskStreamerCore::addWanted(const Plan& currentPlan, juce::int64 start, juce::int64 end,
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
        const auto sourceFrom =
            std::max(juce::int64{0}, segment.sourceStart + (from - segment.start));
        const auto sourceTo =
            std::min((*source)->getLength(), segment.sourceStart + (to - segment.start));
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

bool DiskStreamerCore::load(StreamingAudioSource& source, std::size_t block)
{
    std::unique_ptr<StreamBlock> fresh;
    juce::AudioFormatReader* reader = nullptr;
    {
        const std::scoped_lock bookkeeping(lock);
        if (source.owned[block] != nullptr)
            return true;

        fresh = takeBlock(source.numChannels);
        if (fresh == nullptr)
            return false;
        reader = readerFor(source);
    }

    // The disk, without the bookkeeping lock: a slow file stalls only this thread.
    const bool read = reader != nullptr &&
                      readStream(source, *reader, static_cast<juce::int64>(block) * blockLength,
                                 fresh->samples, fileScratch);

    const std::scoped_lock bookkeeping(lock);
    if (!read)
    {
        freeBlocks.push_back(std::move(fresh));
        return true; // an unreadable block plays as a dropout; carry on with the rest
    }

    fresh->lastWanted = round;
    source.published[block].store(fresh.get());
    source.owned[block] = std::move(fresh);
    return true;
}

std::unique_ptr<StreamBlock> DiskStreamerCore::takeBlock(int numChannels)
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
            if (const auto& block = candidate->owned[index];
                block != nullptr && block->lastWanted < oldest)
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

void DiskStreamerCore::recycleRetired()
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

juce::AudioFormatReader* DiskStreamerCore::readerFor(const StreamingAudioSource& source)
{
    auto& reader = readers[&source];
    if (reader == nullptr)
        reader = settings.openReader(source.file);
    return reader.get();
}

const dsp::Resampling& DiskStreamerCore::resamplingFor(double fileRate)
{
    const std::scoped_lock bookkeeping(lock);
    auto& resampling = resamplings[fileRate];
    if (resampling == nullptr)
        resampling = std::make_unique<dsp::Resampling>(fileRate, settings.sampleRate);
    return *resampling;
}

bool DiskStreamerCore::readStream(const StreamingAudioSource& source,
                                  juce::AudioFormatReader& reader, juce::int64 start,
                                  juce::AudioBuffer<float>& into, juce::AudioBuffer<float>& scratch)
{
    const auto count =
        static_cast<int>(std::min<juce::int64>(into.getNumSamples(), source.length - start));
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

bool DiskStreamerCore::scan(const StreamingAudioSource& source,
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

void DiskStreamerCore::forget(StreamingAudioSource& source)
{
    // Nothing reads a source being destroyed: its blocks can be reused at once.
    const std::scoped_lock bookkeeping(lock);
    std::erase(sources, &source);
    readers.erase(&source);
    for (auto& block : source.owned)
        if (block != nullptr)
            freeBlocks.push_back(std::move(block));
}

bool DiskStreamerCore::hasSources()
{
    const std::scoped_lock bookkeeping(lock);
    return !sources.empty();
}

// DiskStreamer -----------------------------------------------------------------------

class DiskStreamer::Thread final : public juce::Thread
{
public:
    explicit Thread(DiskStreamerCore& coreToRun)
        : juce::Thread("Disk read-ahead")
        , core(coreToRun)
    {
    }

    void run() override
    {
        while (!threadShouldExit())
        {
            core.service();
            wait(5);
        }
    }

private:
    DiskStreamerCore& core;
};

DiskStreamer::DiskStreamer(Settings settings, const RenderClock* clock)
    : core(std::make_shared<DiskStreamerCore>(std::move(settings), clock))
{
    jassert(core->getSettings().sampleRate > 0.0);

    if (core->getSettings().mode == Mode::readAhead)
    {
        thread = std::make_unique<Thread>(*core);
        thread->startThread();
    }
}

DiskStreamer::~DiskStreamer()
{
    if (thread != nullptr)
        thread->stopThread(-1);

    core->stopped.store(true);
    core->dropPlan();
}

double DiskStreamer::getSampleRate() const noexcept
{
    return core->getSettings().sampleRate;
}

DiskStreamer::Mode DiskStreamer::getMode() const noexcept
{
    return core->getSettings().mode;
}

std::shared_ptr<StreamingAudioSource> DiskStreamer::open(const juce::File& file, juce::String& error)
{
    return core->open(core, file, error);
}

void DiskStreamer::setPlan(std::initializer_list<const Arrangement*> arrangements)
{
    core->setPlan(arrangements);
}

void DiskStreamer::followPlayhead(std::function<Playhead()> playhead)
{
    core->followPlayhead(std::move(playhead));
}

void DiskStreamer::service()
{
    core->service();
}

void DiskStreamer::prefetch(juce::int64 start, juce::int64 length)
{
    core->prefetch(start, length);
}

bool DiskStreamer::scan(const StreamingAudioSource& source,
                        const std::function<void(const juce::AudioBuffer<float>&, int)>& consume)
{
    return core->scan(source, consume);
}

std::uint64_t DiskStreamer::getDropouts() const noexcept
{
    return core->dropouts.load();
}

std::size_t DiskStreamer::getMemoryInUse() const noexcept
{
    return core->memoryInUse.load();
}

bool DiskStreamer::hasSources() const
{
    return core->hasSources();
}

} // namespace daw::engine::io
