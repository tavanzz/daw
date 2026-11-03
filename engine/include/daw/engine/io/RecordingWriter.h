#pragma once

#include "daw/engine/dsp/WaveformPeaks.h"
#include "daw/engine/rt/AudioFifo.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <functional>
#include <memory>
#include <mutex>
#include <vector>

namespace daw::engine::io
{

/** Where a recording's samples end up: a WAV file, or in tests a simulated disk. Used
    by the writer thread only. */
class RecordingSink
{
public:
    virtual ~RecordingSink() = default;

    /** Appends numSamples samples of every channel; false if the disk refused them. */
    virtual bool write(const juce::AudioBuffer<float>& audio, int numSamples) = 0;

    /** Makes everything written so far readable after a crash (WAV: rewrites the
        header's lengths). */
    virtual bool flush() = 0;
};

/** A WAV file being recorded at sampleRate: 16 or 24-bit integer (rounded, clipped
    at full scale, no dither) or 32-bit float. Null, with error set, if it cannot be
    created. Destroying the sink completes the file. */
std::unique_ptr<RecordingSink> openWavSink(const juce::File& file, int numChannels,
                                           double sampleRate, int bitsPerSample,
                                           juce::String& error);

struct RecordingWriterSettings
{
    double sampleRate = 48000.0;
    int bitsPerSample = 24;

    /** Seconds of audio each file's FIFO holds: how long the disk may stall. */
    double bufferSeconds = 10.0;

    /** Below this much recording time left on the disk, recording must stop. */
    double minimumSecondsLeft = 10.0;

    /** How often the files are made recoverable and the free space is checked. */
    int checkIntervalMilliseconds = 1000;

    /** Free bytes on the volume holding a file; empty: ask the file system. */
    std::function<juce::int64(const juce::File&)> freeSpace;

    /** Opens a file for writing; empty: openWavSink(). */
    std::function<std::unique_ptr<RecordingSink>(const juce::File&, int numChannels,
                                                 juce::String& error)>
        openSink;
};

/**
    Writes recorded audio to disk on its own thread, so that the audio thread never
    touches a file.

    The message thread adds the files of one recording (addFile()), each with a FIFO
    that the audio thread fills, then calls start(). The writer thread moves
    whatever the FIFOs hold to the files, keeps them recoverable (flush every
    checkIntervalMilliseconds), builds their waveforms and watches the free disk
    space. When the audio thread has pushed its last samples, finish() lets the
    writer empty the FIFOs and complete the files; discard() stops at once and
    deletes them.

    A file that cannot be written (full disk, write error) stops being written and
    the problem is reported; nothing is dropped silently.
*/
class RecordingWriter
{
public:
    enum class Problem
    {
        none,
        lowDiskSpace, ///< less than minimumSecondsLeft of recording time left: stop now
        writeFailed   ///< the disk refused data (full, or removed): the file ends there
    };

    explicit RecordingWriter(RecordingWriterSettings settings);

    /** Stops the thread; files are completed with whatever was written. */
    ~RecordingWriter();

    RecordingWriter(const RecordingWriter&) = delete;
    RecordingWriter& operator=(const RecordingWriter&) = delete;

    /** Message thread, before start(). Returns the FIFO the audio thread fills for the
        file; it lives as long as the writer. */
    rt::AudioFifo& addFile(const juce::File& file, int numChannels);

    /** Message thread. Creates the files and starts writing. On failure nothing is
        written and the files that were created are deleted. */
    juce::Result start();

    /** Any thread, once the audio thread will push nothing more: the writer writes
        what is left, completes the files and stops. */
    void finish() noexcept;

    /** Any thread: true once finish() has completed every file. */
    [[nodiscard]] bool isFinished() const noexcept;

    /** Message thread. Stops at once and deletes the files. */
    void discard();

    [[nodiscard]] int getNumFiles() const noexcept { return static_cast<int>(files.size()); }
    [[nodiscard]] const juce::File& getFile(int index) const;

    /** Any thread. Samples of each channel written to the file so far. */
    [[nodiscard]] juce::int64 getSamplesWritten(int index) const noexcept;

    /** Any thread. The waveform of what has been written (updated with every check). */
    [[nodiscard]] std::shared_ptr<const dsp::WaveformPeaks> getPeaks(int index) const;

    [[nodiscard]] Problem getProblem() const noexcept { return problem.load(); }

    /** Any thread. Recording time left on the disk of the first file at the rate all
        the files are written, as of the last check; negative before the first. */
    [[nodiscard]] double getSecondsLeft() const noexcept { return secondsLeft.load(); }

    /** Bytes per second of numChannels channels at sampleRate and bitsPerSample. */
    [[nodiscard]] static double bytesPerSecond(double sampleRate, int bitsPerSample,
                                               int numChannels) noexcept;

private:
    struct OutputFile
    {
        OutputFile(const juce::File& fileToWrite, int numChannels, int capacity)
            : file(fileToWrite)
            , fifo(numChannels, capacity)
            , peaks(numChannels)
        {
        }

        juce::File file;
        rt::AudioFifo fifo;
        std::unique_ptr<RecordingSink> sink;
        dsp::WaveformPeaks::Builder peaks;
        std::atomic<juce::int64> samplesWritten{0};
        bool failed = false;
    };

    class Thread;

    void run();
    bool moveAudio();
    void check();
    void publishPeaks();
    void closeFiles();

    RecordingWriterSettings settings;
    std::vector<std::unique_ptr<OutputFile>> files;
    juce::AudioBuffer<float> scratch;
    std::vector<float*> scratchChannels;

    std::atomic<bool> finishing{false};
    std::atomic<bool> finished{false};
    std::atomic<Problem> problem{Problem::none};
    std::atomic<double> secondsLeft{-1.0};

    mutable std::mutex peaksLock;
    std::vector<std::shared_ptr<const dsp::WaveformPeaks>> publishedPeaks;

    std::unique_ptr<Thread> thread;
};

} // namespace daw::engine::io
