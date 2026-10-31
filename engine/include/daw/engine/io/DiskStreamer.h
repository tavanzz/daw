#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/AudioSource.h"
#include "daw/engine/RenderClock.h"
#include "daw/engine/dsp/Resampler.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <vector>

namespace daw::engine::io
{

class DiskStreamer;

/** One block of a streamed file: blockLength samples of every channel, at the
    streamer's rate. */
struct StreamBlock
{
    juce::AudioBuffer<float> samples;
    std::uint64_t lastWanted = 0; ///< the read-ahead round that last needed it
};

/**
    A file played from disk: an AudioSource whose samples live in blocks that its
    DiskStreamer loads (and converts to the streamer's rate) ahead of the playhead.

    read() copies from loaded blocks only; where a block is missing it writes
    silence, counts a dropout and returns false: it never waits for the disk (except
    in a synchronous streamer, for offline rendering, which loads what it misses).

    Created by DiskStreamer::open() on the message thread; destroyed off the audio
    thread like any source.
*/
class StreamingAudioSource final : public AudioSource
{
public:
    ~StreamingAudioSource() override;

    [[nodiscard]] int getNumChannels() const noexcept override { return numChannels; }
    [[nodiscard]] juce::int64 getLength() const noexcept override { return length; }
    [[nodiscard]] double getSampleRate() const noexcept override;

    bool read(int channel, juce::int64 start, float* destination,
              int numSamples) const noexcept override;
    [[nodiscard]] bool isReady(juce::int64 start, juce::int64 numSamples) const noexcept override;

    [[nodiscard]] const juce::File& getFile() const noexcept { return file; }
    [[nodiscard]] double getFileSampleRate() const noexcept { return fileSampleRate; }
    [[nodiscard]] juce::int64 getFileLength() const noexcept { return fileLength; }

private:
    friend class DiskStreamer;

    StreamingAudioSource(DiskStreamer& owner, juce::File source, int channels,
                         double sourceRate, juce::int64 sourceLength, juce::int64 streamLength);

    [[nodiscard]] std::size_t getNumBlocks() const noexcept { return owned.size(); }

    DiskStreamer& streamer;
    const juce::File file;
    const int numChannels;
    const double fileSampleRate;
    const juce::int64 fileLength;
    const juce::int64 length;

    // Published blocks, read by the audio thread; owned blocks, changed only by the
    // thread that loads (the streamer's, or a synchronous reader under its lock).
    std::unique_ptr<std::atomic<const StreamBlock*>[]> published;
    std::vector<std::unique_ptr<StreamBlock>> owned;
};

/**
    Streams audio files from disk for playback: the blocks the timeline is about to
    play are loaded ahead of the playhead, so the audio thread never reads a file.

    - Blocks are blockLength samples per channel. A fixed memory budget holds them;
      beyond it the least recently needed blocks are dropped, so a small project ends
      up wholly in memory and a large one streams.
    - What to load comes from the plan (the segments of the arrangement playing,
      setPlan()) and the playhead (followPlayhead()): lookaheadSeconds from the
      playhead, the start of the loop region when looping, and a second from where
      playback would start while stopped.
    - Files at another rate are converted while loading, with the same filter and
      arithmetic as dsp::resample(): streamed audio is identical to the bit to the
      whole file converted in memory.
    - A block dropped from the plan may still be read by the audio block in progress:
      its memory is reused only once the RenderClock says that block is over.

    Modes: readAhead runs its own thread; manual leaves the rounds to service() (for
    tests); synchronous loads whatever read() misses, on the reading thread (offline
    rendering, where waiting is fine).

    Message thread for open(), setPlan() and followPlayhead(); see each function.
*/
class DiskStreamer
{
public:
    static constexpr int blockLength = 1 << 15;

    enum class Mode
    {
        readAhead,
        manual,
        synchronous
    };

    /** What the read-ahead follows. */
    struct Playhead
    {
        juce::int64 position = 0;
        bool playing = false;
        bool looping = false;
        juce::int64 loopStart = 0;
        juce::int64 loopEnd = 0;
    };

    struct Settings
    {
        double sampleRate = 48000.0;
        Mode mode = Mode::readAhead;
        std::size_t memoryBudget = std::size_t{512} << 20;
        double lookaheadSeconds = 3.0;

        /** How files are opened; empty: WAV and AIFF readers. For tests (slow disks). */
        std::function<std::unique_ptr<juce::AudioFormatReader>(const juce::File&)> openReader;
    };

    /** clock: the engine's, for reusing memory safely; may be null for synchronous
        mode (one thread reads and loads). */
    DiskStreamer(Settings settings, const RenderClock* clock);

    /** Stops the thread. Sources must not be read any more. */
    ~DiskStreamer();

    DiskStreamer(const DiskStreamer&) = delete;
    DiskStreamer& operator=(const DiskStreamer&) = delete;

    [[nodiscard]] double getSampleRate() const noexcept { return settings.sampleRate; }
    [[nodiscard]] Mode getMode() const noexcept { return settings.mode; }

    /** Any thread but the audio thread. A source for the file at the streamer's
        rate; null with error set if it cannot be read. Reads only its header. */
    std::shared_ptr<StreamingAudioSource> open(const juce::File& file, juce::String& error);

    /** Message thread. What is playing: the read-ahead loads its segments. */
    void setPlan(const Arrangement& arrangement);

    /** Message thread, before playback. Where the playhead is, asked by the
        read-ahead every round (it must be safe to call from that thread). */
    void followPlayhead(std::function<Playhead()> playhead);

    /** One round of loading (the read-ahead thread does this on its own). */
    void service();

    /** Loads [start, start + length) of what the plan plays there now, before
        returning (seeks in tests; a preview when stopped). */
    void prefetch(juce::int64 start, juce::int64 length);

    /** Any thread. Reads that found a block missing, since the streamer started. */
    [[nodiscard]] std::uint64_t getDropouts() const noexcept { return dropouts.load(); }

    /** Any thread. Bytes of audio held in blocks. */
    [[nodiscard]] std::size_t getMemoryInUse() const noexcept { return memoryInUse.load(); }

    /** Reads a whole source at the streamer's rate, block by block, for its waveform
        (the loading threads of a source pool). Calls consume with each block. */
    bool scan(const StreamingAudioSource& source,
              const std::function<void(const juce::AudioBuffer<float>&, int)>& consume);

private:
    friend class StreamingAudioSource;

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

    class Thread;

    void run();
    void forget(StreamingAudioSource& source);
    void loadSynchronously(const StreamingAudioSource& source, std::size_t block);
    std::vector<Wanted> wantedBlocks(const Plan& plan, const Playhead& playhead) const;
    void addWanted(const Plan& plan, juce::int64 start, juce::int64 end,
                   std::vector<Wanted>& wanted) const;
    bool load(StreamingAudioSource& source, std::size_t block);
    std::unique_ptr<StreamBlock> takeBlock(int numChannels);
    void recycleRetired();
    bool readBlock(const StreamingAudioSource& source, juce::AudioFormatReader& reader,
                   std::size_t block, StreamBlock& into);
    bool readStream(const StreamingAudioSource& source, juce::AudioFormatReader& reader,
                    juce::int64 start, juce::AudioBuffer<float>& into,
                    juce::AudioBuffer<float>& scratch);
    juce::AudioFormatReader* readerFor(const StreamingAudioSource& source);
    const dsp::Resampling& resamplingFor(double fileRate);

    Settings settings;
    const RenderClock* clock;

    // Plan and playhead: set by the message thread, read by the loading thread.
    mutable std::mutex planLock;
    std::shared_ptr<const Plan> plan;
    std::function<Playhead()> playhead;

    // One loader at a time (the read-ahead thread, a prefetch, a synchronous read):
    // it owns the readers in use and fileScratch, and does the disk work.
    std::mutex loadingLock;

    // Bookkeeping, held briefly (never during disk work): sources, blocks, readers.
    std::recursive_mutex loadLock;
    std::vector<StreamingAudioSource*> sources; // every live source, for eviction
    std::vector<std::unique_ptr<StreamBlock>> freeBlocks;
    struct Retired
    {
        std::unique_ptr<StreamBlock> block;
        std::uint64_t ticket = 0;
    };
    std::vector<Retired> retired;
    std::uint64_t round = 0;
    std::map<const StreamingAudioSource*, std::unique_ptr<juce::AudioFormatReader>> readers;
    std::map<double, std::unique_ptr<dsp::Resampling>> resamplings;
    juce::AudioBuffer<float> fileScratch;

    std::atomic<std::uint64_t> dropouts{0};
    std::atomic<std::size_t> memoryInUse{0};

    std::unique_ptr<Thread> thread;
};

} // namespace daw::engine::io
