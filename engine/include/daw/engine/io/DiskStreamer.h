#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/AudioSource.h"
#include "daw/engine/RenderClock.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <atomic>
#include <cstdint>
#include <functional>
#include <initializer_list>
#include <memory>
#include <vector>

namespace daw::engine::io
{

class DiskStreamerCore;

/** One block of a streamed file: DiskStreamer::blockLength samples of every channel,
    at the streamer's rate. */
struct StreamBlock
{
    juce::AudioBuffer<float> samples;
    std::uint64_t lastWanted = 0; ///< the loading round that last needed it
};

/**
    A file played from disk: an AudioSource whose samples live in blocks that its
    DiskStreamer loads (and converts to the streamer's rate) ahead of the playhead.

    read() copies from loaded blocks only; where a block is missing it writes
    silence, counts a dropout and returns false: it never waits for the disk (except
    in a synchronous streamer, for offline rendering, which loads what it misses).

    Created by DiskStreamer::open(); destroyed off the audio thread like any source.
    It keeps the streamer's state alive, so it may outlive the streamer itself.
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
    [[nodiscard]] const juce::String& getFormatName() const noexcept { return formatName; }
    [[nodiscard]] int getFileBitsPerSample() const noexcept { return fileBitsPerSample; }

private:
    friend class DiskStreamerCore;

    struct FileInfo
    {
        juce::File file;
        int numChannels = 0;
        double sampleRate = 0.0;
        juce::int64 length = 0;
        juce::String formatName;
        int bitsPerSample = 0;
    };

    StreamingAudioSource(std::shared_ptr<DiskStreamerCore> owner, const FileInfo& info,
                         juce::int64 streamLength);

    const std::shared_ptr<DiskStreamerCore> core;
    const juce::File file;
    const int numChannels;
    const double fileSampleRate;
    const juce::int64 fileLength;
    const juce::int64 length;
    const juce::String formatName;
    const int fileBitsPerSample;

    // Published blocks, read by the audio thread; owned blocks, changed only by the
    // loading side of the streamer.
    std::unique_ptr<std::atomic<const StreamBlock*>[]> published;
    std::vector<std::unique_ptr<StreamBlock>> owned;
};

/**
    Streams audio files from disk for playback: the blocks the timeline is about to
    play are loaded ahead of the playhead, so the audio thread never reads a file.

    - Blocks are blockLength samples per channel. A fixed memory budget holds them;
      beyond it the least recently needed blocks are dropped, so a small project ends
      up wholly in memory and a large one streams.
    - What to load comes from the plan (the segments of the arrangements playing or
      about to, setPlan()) and the playhead (followPlayhead()): lookaheadSeconds from
      the playhead, the start of the loop region when looping, and a second from
      where playback would start while stopped.
    - Files at another rate are converted while loading, with the same filter and
      arithmetic as dsp::resample(): streamed audio is identical to the bit to the
      whole file converted in memory.
    - A block dropped from memory may still be read by the audio block in progress:
      it is reused only once the RenderClock says that block is over.

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

        /** Where playback was just asked to jump (-1 for nowhere): loaded first. */
        juce::int64 cue = -1;
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

    /** clock: the engine's, for reusing memory safely; null in synchronous mode
        (one thread both reads and loads). It must outlive the streamer. */
    DiskStreamer(Settings settings, const RenderClock* clock);

    /** Stops the read-ahead. Sources may live on (they play silence from then on,
        except what is already loaded). */
    ~DiskStreamer();

    DiskStreamer(const DiskStreamer&) = delete;
    DiskStreamer& operator=(const DiskStreamer&) = delete;

    [[nodiscard]] double getSampleRate() const noexcept;
    [[nodiscard]] Mode getMode() const noexcept;

    /** Any thread but the audio thread. A source for the file at the streamer's
        rate; null with error set if it cannot be read. Reads only its header. */
    std::shared_ptr<StreamingAudioSource> open(const juce::File& file, juce::String& error);

    /** Message thread. What is playing, and what is about to (an arrangement waiting
        for its data): the read-ahead loads their segments. */
    void setPlan(std::initializer_list<const Arrangement*> arrangements);
    void setPlan(const Arrangement& arrangement) { setPlan({&arrangement}); }

    /** Message thread, before playback. Where the playhead is; called by the
        read-ahead every round, so it must be safe from that thread. */
    void followPlayhead(std::function<Playhead()> playhead);

    /** One round of loading (the read-ahead thread does this on its own). */
    void service();

    /** Loads [start, start + length) of what the plan plays there, before returning. */
    void prefetch(juce::int64 start, juce::int64 length);

    /** Reads a whole source at the streamer's rate, a block at a time, calling
        consume with each (for its waveform). Any thread but the audio thread. */
    bool scan(const StreamingAudioSource& source,
              const std::function<void(const juce::AudioBuffer<float>&, int)>& consume);

    /** Any thread. Reads that found a block missing, since the streamer started. */
    [[nodiscard]] std::uint64_t getDropouts() const noexcept;

    /** Any thread. Bytes of audio held in blocks. */
    [[nodiscard]] std::size_t getMemoryInUse() const noexcept;

    /** Any thread. Whether any source of this streamer is still alive. */
    [[nodiscard]] bool hasSources() const;

private:
    class Thread;

    std::shared_ptr<DiskStreamerCore> core;
    std::unique_ptr<Thread> thread;
};

} // namespace daw::engine::io
