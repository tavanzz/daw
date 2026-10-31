#pragma once

#include "daw/controller/ClipEditing.h"
#include "daw/controller/LatencyMeasurement.h"
#include "daw/controller/RecordingController.h"
#include "daw/controller/RecordingJournal.h"
#include "daw/engine/Engine.h"
#include "daw/engine/io/SourcePool.h"
#include "daw/model/Project.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <atomic>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace daw::controller
{

/** How a Session plays its files. */
struct SessionOptions
{
    /** readAhead for the app; manual for tests, where update() runs the read-ahead
        between audio blocks so that streaming is deterministic. */
    engine::io::DiskStreamer::Mode streaming = engine::io::DiskStreamer::Mode::readAhead;
    std::size_t streamingMemory = std::size_t{512} << 20;
};

/**
    One open project and everything around it: its undo history, the audio its clips
    use, and the engine that plays it. Every user interface edits the project through
    a Session, so the editing rules, undo steps and the way the engine follows the
    project are the same for all of them.

    - Edits are undoable, one step each, except loop on/off, snap and grid (as the
      model decides). A gesture (a fader drag, a clip drag) groups every edit made
      between beginGesture() and endGesture() into one step.
    - After any change (an edit, undo, redo, a file finishing loading) the engine
      gets a new arrangement at the next update(), so many changes in a row cost one.
      While playing, a new arrangement waits (up to maximumArrangementWait) until the
      streamed audio it plays at the playhead is loaded, so edits cause no dropouts.

    Message thread only. update() must be called regularly (a UI timer).
*/
class Session : private juce::ValueTree::Listener
{
public:
    /** Used while no audio device runs; files are reloaded at the device's rate. */
    static constexpr double fallbackSampleRate = 48000.0;

    static constexpr double maximumArrangementWait = 0.5; // seconds

    explicit Session(engine::Engine& engineToDrive, SessionOptions options = {});
    ~Session() override;

    Session(const Session&) = delete;
    Session& operator=(const Session&) = delete;

    /** Applies finished file loads, gives the engine a new arrangement if the project
        changed, and frees memory the engine let go of. */
    void update();

    /** Blocks until no file is loading (or the timeout passes), then update()s.
        For tests and command-line tools; a UI just keeps calling update(). */
    bool waitForLoads(int timeoutMilliseconds);

    /** The audio device runs at a new rate: files are converted again. */
    void setSampleRate(double sampleRate);
    [[nodiscard]] double getSampleRate() const noexcept { return sources.getSampleRate(); }

    // Project files ------------------------------------------------------------------

    void newProject();
    /** On failure the current project stays open; the error is written for the user. */
    juce::Result openProject(const juce::File& file);
    juce::Result saveProject(const juce::File& file);
    [[nodiscard]] juce::File getProjectFile() const { return projectFile; }
    [[nodiscard]] bool hasUnsavedChanges() const noexcept { return unsavedChanges; }

    /** Goes up whenever anything a view shows may have changed: an edit, undo, a
        file finishing loading, another project. Views compare it to redraw. */
    [[nodiscard]] juce::uint64 getRevision() const noexcept { return revision; }

    // Reading the project -------------------------------------------------------------

    /** For views: change the project only through the Session. */
    [[nodiscard]] const model::Project& getProject() const noexcept { return project; }

    /** The audio of a file (status, waveform, details); null if no clip uses it. */
    [[nodiscard]] const engine::io::PooledSource* findSource(const juce::File& file) const;

    /** Length of a loaded file in ticks; nothing while it loads or if it failed. */
    [[nodiscard]] std::optional<model::Ticks> getFileLength(const juce::File& file) const;

    /** Times streamed audio was missing when the engine needed it, since the last
        call: the disk is too slow (or the project too large for it). */
    [[nodiscard]] std::uint64_t takeDropouts();

    /** The meter of a track, measured after its fader. */
    [[nodiscard]] std::shared_ptr<engine::dsp::PeakLevelMeter>
    getTrackMeter(model::ObjectId trackId);

    /** Input meters. The engine's input peaks have one reader: the view refreshing
        the meters calls takeInputPeaks() once per refresh, then reads each track's
        level (or each device channel's) as of that call. */
    void takeInputPeaks();
    [[nodiscard]] float getInputPeak(int channel) const noexcept;
    /** Left and right input level of a track (a mono input gives the same on both);
        0 for a track without an input. */
    [[nodiscard]] std::array<float, 2> getTrackInputPeaks(model::ObjectId trackId) const;

    /** Snapping as set in the project, at the zoom level given (for the automatic
        grid); bypass turns it off for one drag. */
    [[nodiscard]] Snapper makeSnapper(double ticksPerPixel, bool bypass) const;

    // Edits -----------------------------------------------------------------------------

    void beginGesture(const juce::String& name);
    void endGesture();

    /** Adds a track at the end; an empty name becomes "Track n". Returns its id. */
    model::ObjectId addTrack(const juce::String& name = {});
    void removeTrack(model::ObjectId trackId);
    void renameTrack(model::ObjectId trackId, const juce::String& name);
    void setTrackVolume(model::ObjectId trackId, double gain);
    void setTrackPan(model::ObjectId trackId, double pan);
    void setTrackMuted(model::ObjectId trackId, bool muted);
    void setTrackSoloed(model::ObjectId trackId, bool soloed);
    void setTrackInput(model::ObjectId trackId, model::TrackInput input);
    void setTrackMonitoring(model::ObjectId trackId, model::MonitoringMode mode);
    void setMasterVolume(double gain);

    /** Adds audio files as clips starting at position: one file onto an existing
        track if ontoTrack is given, otherwise one new track per file (named after
        it). One undo step. Returns the new clips' ids. */
    std::vector<model::ObjectId> importAudio(const juce::Array<juce::File>& files,
                                             model::Ticks position,
                                             std::optional<model::ObjectId> ontoTrack = {});

    void moveClip(model::ObjectId clipId, model::ObjectId toTrackId, model::Ticks position);
    void setClipBounds(model::ObjectId clipId, const model::ClipBounds& bounds);
    void deleteClips(const std::vector<model::ObjectId>& clipIds);

    void setTempo(double quarterNotesPerMinute);
    void setTimeSignature(model::TimeSignature timeSignature);
    void setLoopRange(model::Ticks start, model::Ticks end);

    // Not undoable.
    void setLoopEnabled(bool shouldLoop);
    void setSnapEnabled(bool shouldSnap);
    void setGridDivision(model::GridDivision division);
    void setTrackArmed(model::ObjectId trackId, bool armed);
    void setMetronomeEnabled(bool shouldClick);
    void setMetronomeVolume(double gain);
    void setCountInEnabled(bool shouldCountIn);
    void setCountInBars(int bars);
    void setPunchEnabled(bool shouldPunch);
    void setRecordingBitsPerSample(int bitsPerSample);

    bool undo();
    bool redo();
    [[nodiscard]] bool canUndo() const { return undoManager.canUndo(); }
    [[nodiscard]] bool canRedo() const { return undoManager.canRedo(); }
    [[nodiscard]] juce::String getUndoDescription() const;
    [[nodiscard]] juce::String getRedoDescription() const;

    // Recording ---------------------------------------------------------------------

    /** Records every armed track that has an input, from the playhead (after the
        count-in, if it is on; with punch on, keeping only the loop region), until
        stop(). The files go into the "Audio" folder next to the project, which must
        have been saved. Fails with a message for the user (unsaved project, nothing
        armed, too little disk space...). The clips appear, in one undo step, at an
        update() once the files are complete. */
    juce::Result record();

    /** Stops the recording and deletes its files: no clip is added. */
    void discardRecording();

    /** From record() until its clips are added (or it is discarded). */
    [[nodiscard]] bool isRecording() const noexcept { return recording.isActive(); }
    [[nodiscard]] const RecordingController& getRecording() const noexcept { return recording; }
    [[nodiscard]] RecordingController& getRecording() noexcept { return recording; }

    /** Why the last recording ended early, for the user (empty if it did not);
        reading it clears it. */
    [[nodiscard]] juce::String takeRecordingProblem();

    /** Recordings found when the project was opened that its file does not have:
        interrupted by a crash, or never saved before one. The UI offers to recover
        or discard them. */
    [[nodiscard]] const std::vector<JournalEntry>& getRecoverableRecordings() const noexcept
    {
        return recoverable;
    }

    /** Repairs the recoverable recordings' files and adds them as clips (on their
        tracks, or on new ones if those are gone), in one undo step. Returns a message
        naming any file that could not be repaired. */
    juce::Result recoverRecordings();

    /** Deletes the recoverable recordings' files. */
    void discardRecoverableRecordings();

    /** Latency compensation: what the device reports for its input and output, plus
        a manual offset (the result of measuring with a cable); all in samples. */
    void setDeviceLatency(int inputSamples, int outputSamples);
    void setLatencyOffset(int samples);
    [[nodiscard]] int getLatencyOffset() const noexcept { return latencyOffset; }
    [[nodiscard]] juce::int64 getRecordingLatency() const noexcept;

    /** Measures the round trip with a cable from output outputChannel (0 or 1) to
        input inputChannel; stops playback first. Not while recording. The result
        comes from takeLatencyResult() after a few seconds of update()s; applying
        its offset is up to the caller. */
    juce::Result measureLatency(int outputChannel, int inputChannel);
    [[nodiscard]] bool isMeasuringLatency() const noexcept { return latencyMeasurement.isRunning(); }
    [[nodiscard]] float getLatencyMeasurementProgress() const noexcept
    {
        return latencyMeasurement.getProgress();
    }
    [[nodiscard]] std::optional<LatencyMeasurement::Result> takeLatencyResult();

    // Transport, in ticks ---------------------------------------------------------------

    void play();
    void pause();
    void stop();
    void seek(model::Ticks position);
    [[nodiscard]] model::Ticks getPlayhead() const;
    [[nodiscard]] engine::TransportState getTransportState() const;

    [[nodiscard]] engine::Engine& getEngine() noexcept { return engine; }

private:
    void startEdit(const juce::String& name);
    void replaceProject(model::Project newProject);
    void addRecording(const RecordingOutcome& outcome);
    void copyRecordingsTo(const juce::File& newProjectDirectory);
    void writeJournal();
    void leaveProject();
    void requestAllSources();
    void sourceChanged(const juce::File& file);
    void resolveLengths(const juce::File& file);
    void rebuildArrangement();
    void handOver(std::unique_ptr<engine::Arrangement> arrangement);
    void streamingTick();
    [[nodiscard]] double engineRate() const noexcept;
    [[nodiscard]] std::optional<model::Track> findTrack(model::ObjectId trackId) const;

    void projectChanged();
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override;

    engine::Engine& engine;
    SessionOptions options;
    engine::io::SourcePool sources;
    std::unique_ptr<engine::Arrangement> pendingArrangement; // waits for its audio
    std::unique_ptr<engine::Arrangement> playingArrangement; // a copy of the engine's, for the plan
    juce::uint32 pendingSince = 0;
    std::uint64_t dropoutsSeen = 0;
    std::atomic<juce::int64> seekCue{-1}; // the last seek, for the read-ahead
    juce::UndoManager undoManager{10'000'000, 200};
    model::Project project;
    juce::File projectFile;
    std::map<model::ObjectId, std::shared_ptr<engine::dsp::PeakLevelMeter>> meters;
    std::array<float, engine::AudioInputs::maxChannels> inputPeaks{};

    RecordingController recording{engine};
    juce::String recordingProblem;
    std::vector<JournalEntry> journal;     // this project's recordings not saved yet
    std::vector<JournalEntry> recoverable; // found on opening it
    LatencyMeasurement latencyMeasurement{engine};
    std::optional<LatencyMeasurement::Result> latencyResult;
    int deviceInputLatency = 0;
    int deviceOutputLatency = 0;
    int latencyOffset = 0;

    juce::uint64 revision = 1;
    bool arrangementOutOfDate = true;
    bool unsavedChanges = false;
    bool inGesture = false;
    bool resolvingLengths = false;
};

} // namespace daw::controller
