#pragma once

#include "daw/engine/Engine.h"
#include "daw/engine/io/RecordingWriter.h"

#include <juce_core/juce_core.h>

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace daw::controller
{

/** The folder next to a project file where its recordings go. */
inline constexpr const char* recordingsFolderName = "Audio";

/** One track to record: its device inputs. */
struct TrackToRecord
{
    juce::int64 trackId = 0;
    juce::String name; ///< names its files
    int inputChannel = 0;
    int numInputChannels = 1;
};

/** Everything one recording needs. Positions in samples at sampleRate, the device's. */
struct RecordingRequest
{
    juce::File folder; ///< where the files go; created if missing
    double sampleRate = 48000.0;
    int bitsPerSample = 24;
    std::vector<TrackToRecord> tracks;

    juce::int64 startPosition = 0;
    juce::int64 countInLength = 0;
    juce::int64 latency = 0;

    /** A punch range when punchOut > punchIn. */
    juce::int64 punchIn = 0;
    juce::int64 punchOut = 0;
};

/** One track's recording: its file, and where the file's first sample belongs. */
struct RecordedTake
{
    juce::int64 trackId = 0;
    juce::File file;
    juce::int64 firstPosition = 0;
    juce::int64 length = 0; ///< samples in the file
};

/** How a recording ended. */
struct RecordingOutcome
{
    std::vector<RecordedTake> takes; ///< none if it was discarded

    /** What the recording kept on the timeline: [keepStart, keepEnd), the punch
        range if there was one. Takes may hold more (handles). */
    juce::int64 keepStart = 0;
    juce::int64 keepEnd = 0;

    /** Why it ended early, for the user; empty if the user stopped it. */
    juce::String problem;
};

/**
    Runs one recording at a time: creates the files, hands the inputs to the engine
    and the writer thread, stops on problems (the disk falling behind or filling up,
    the device stopping) and reports what was recorded. Knows nothing of the project:
    Session turns requests into recordings and outcomes into clips.

    Message thread only. update() must be called regularly (the UI timer).
*/
class RecordingController
{
public:
    enum class State
    {
        idle,
        countingIn,
        recording,
        finishing ///< stopped: the last samples are being captured and written
    };

    /** A recording needs this much recording time on the disk to start, and the
        user is warned below lowSpaceWarningSeconds. */
    static constexpr double minimumSecondsToStart = 60.0;
    static constexpr double lowSpaceWarningSeconds = 600.0;

    explicit RecordingController(engine::Engine& engineToUse);

    /** Stops a recording in progress, keeping what was written. */
    ~RecordingController();

    RecordingController(const RecordingController&) = delete;
    RecordingController& operator=(const RecordingController&) = delete;

    /** Starts a recording; fails, with a message for the user, if one is running,
        nothing is to be recorded, the folder or the files cannot be created or the
        disk is too full. */
    juce::Result start(const RecordingRequest& request);

    /** Ends the recording; the outcome comes from update() once everything is written. */
    void stop();

    /** Ends the recording and deletes its files; update() reports no takes. */
    void discard();

    /** Moves the recording on. Returns its outcome once, when it is over. */
    std::optional<RecordingOutcome> update();

    [[nodiscard]] State getState() const;
    [[nodiscard]] bool isActive() const noexcept { return writer != nullptr; }

    /** Recording time left on the disk at the current rate; negative if unknown. */
    [[nodiscard]] double getSecondsLeft() const;

    /** One track being recorded, for drawing it as it grows. */
    struct LiveTake
    {
        juce::int64 trackId = 0;
        juce::File file;
        juce::int64 firstPosition = 0; ///< where the file's first sample belongs
        juce::int64 keepStart = 0;     ///< where the clip it becomes will start
        juce::int64 length = 0;
        std::shared_ptr<const engine::dsp::WaveformPeaks> peaks;
    };
    [[nodiscard]] std::vector<LiveTake> getLiveTakes() const;

    /** Free bytes on the disk of a folder; empty: ask the file system. For tests. */
    std::function<juce::int64(const juce::File&)> freeSpace;

    /** Changes the writer's settings before it starts. For tests (slow disks). */
    std::function<void(engine::io::RecordingWriterSettings&)> adjustWriterSettings;

private:
    [[nodiscard]] juce::int64 bytesFree(const juce::File& folder) const;
    void endWithProblem(const juce::String& message);
    void reset();

    engine::Engine& engine;
    RecordingRequest request;
    std::unique_ptr<engine::RecordingSetup> setup;
    std::unique_ptr<engine::io::RecordingWriter> writer;
    bool stopRequested = false;
    bool finishRequested = false;
    bool discarding = false;
    juce::String problem;
};

} // namespace daw::controller
