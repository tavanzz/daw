#pragma once

#include "daw/controller/ClipEditing.h"
#include "daw/engine/Engine.h"
#include "daw/engine/io/SourcePool.h"
#include "daw/model/Project.h"

#include <juce_data_structures/juce_data_structures.h>

#include <array>
#include <map>
#include <memory>
#include <optional>
#include <vector>

namespace daw::controller
{

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

    Message thread only. update() must be called regularly (a UI timer).
*/
class Session : private juce::ValueTree::Listener
{
public:
    /** Used while no audio device runs; files are reloaded at the device's rate. */
    static constexpr double fallbackSampleRate = 48000.0;

    explicit Session(engine::Engine& engineToDrive);
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
    void requestAllSources();
    void sourceChanged(const juce::File& file);
    void resolveLengths(const juce::File& file);
    void rebuildArrangement();
    [[nodiscard]] double engineRate() const noexcept;
    [[nodiscard]] std::optional<model::Track> findTrack(model::ObjectId trackId) const;

    void projectChanged();
    void valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&) override;
    void valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&) override;
    void valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int) override;
    void valueTreeChildOrderChanged(juce::ValueTree&, int, int) override;

    engine::Engine& engine;
    engine::io::SourcePool sources{fallbackSampleRate};
    juce::UndoManager undoManager{10'000'000, 200};
    model::Project project;
    juce::File projectFile;
    std::map<model::ObjectId, std::shared_ptr<engine::dsp::PeakLevelMeter>> meters;
    std::array<float, engine::AudioInputs::maxChannels> inputPeaks{};

    juce::uint64 revision = 1;
    bool arrangementOutOfDate = true;
    bool unsavedChanges = false;
    bool inGesture = false;
    bool resolvingLengths = false;
};

} // namespace daw::controller
