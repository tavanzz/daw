#pragma once

#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/ChainOwner.h"
#include "daw/controller/ClipEditing.h"
#include "daw/controller/EffectInstances.h"
#include "daw/controller/FadeOutKeeper.h"
#include "daw/controller/LatencyMeasurement.h"
#include "daw/controller/RecordingController.h"
#include "daw/controller/RecordingJournal.h"
#include "daw/controller/SampleCache.h"
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

/** What the user is told when the inputs may not be used. */
inline constexpr const char* microphoneDeniedMessage =
    "Microphone access is off for this app, so it cannot record or hear any input. "
    "Allow it in System Settings > Privacy & Security > Microphone, then try again.";

/** Why an effect edit was refused while recording. */
inline constexpr const char* latencyWhileRecordingMessage =
    "Effects that delay the audio (such as the limiter) cannot be added, moved or removed "
    "while recording: the recording would no longer line up. Stop recording first.";

/** Where an effect of the project is: in an owner's chain, or a track's instrument. */
struct EffectLocation
{
    ChainOwner owner;
    int index = -1; ///< -1 for an instrument
    [[nodiscard]] bool isInstrument() const noexcept { return index < 0; }
};

/** How a Session plays its files. */
struct SessionOptions
{
    /** readAhead for the app; manual for tests, where update() runs the read-ahead
        between audio blocks so that streaming is deterministic. */
    engine::io::DiskStreamer::Mode streaming = engine::io::DiskStreamer::Mode::readAhead;
    std::size_t streamingMemory = std::size_t{512} << 20;

    /** Effects and instruments beyond the built-in ones (tests). */
    EffectFactory effects = {};

    /** Plugins: how to instantiate them and where to find them. Without both, plugin
        effects are left out (and kept in the project as they are). */
    PluginLoader* plugins = nullptr;
    const PluginCatalogue* catalogue = nullptr;
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

    /** The block size effects are prepared for (longer blocks are processed in
        pieces). */
    static constexpr int effectBlockSize = 512;

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

    /** The instrument new MIDI tracks get. */
    static constexpr const char* defaultInstrument = "synth";

    /** Adds a MIDI track at the end with an instrument of this type (none if empty
        or unknown); an empty name becomes "MIDI n". One undo step. Returns its id. */
    model::ObjectId addMidiTrack(const juce::String& name = {},
                                 const juce::String& instrumentType = defaultInstrument);
    void setTrackMidiInput(model::ObjectId trackId, const model::MidiInput& input);
    void setTrackMidiChannel(model::ObjectId trackId, int channel);

    /** Gives a MIDI track an instrument of this type, replacing the one it has (and
        the automation of its parameters). Returns its id; 0 if the track is not a MIDI
        track or the type is not an instrument. */
    model::ObjectId setInstrument(model::ObjectId trackId, const juce::String& type);
    void removeInstrument(model::ObjectId trackId);
    void removeTrack(model::ObjectId trackId);
    void renameTrack(model::ObjectId trackId, const juce::String& name);
    void setTrackVolume(model::ObjectId trackId, double gain);
    void setTrackPan(model::ObjectId trackId, double pan);
    void setTrackMuted(model::ObjectId trackId, bool muted);
    void setTrackSoloed(model::ObjectId trackId, bool soloed);
    void setTrackInput(model::ObjectId trackId, model::TrackInput input);
    void setTrackMonitoring(model::ObjectId trackId, model::MonitoringMode mode);
    void setMasterVolume(double gain);

    // Effects, buses and sends -----------------------------------------------------------

    /** Adds a built-in effect to an owner's chain at index (-1: the end). Delay and
        reverb start fully wet on a bus. Returns its id; 0 if the type is unknown, the
        owner does not exist or the edit is refused (see takeEditProblem()). */
    model::ObjectId addEffect(const ChainOwner& owner, const juce::String& type, int index = -1);
    bool removeEffect(model::ObjectId effectId);
    /** Within its chain. */
    bool moveEffect(model::ObjectId effectId, int toIndex);
    void setEffectBypassed(model::ObjectId effectId, bool bypassed);
    /** In the parameter's units; a drag is one gesture. */
    void setEffectParameter(model::ObjectId effectId, const juce::String& parameterId,
                            double value);
    /** An effect silenced for a NaN or an infinity plays again, from a clean state.
        Not undoable. */
    void reviveEffect(model::ObjectId effectId);

    /** Gives an instrument a file to play in one of its sample slots (a sampler's
        "sample", a drum pad's "padN"); an empty file removes it. One undo step. */
    void setInstrumentFile(model::ObjectId effectId, const juce::String& key,
                           const juce::File& file);

    /** The samples instruments play: loaded (find()), loading or failed (getStatus(),
        getError()). */
    [[nodiscard]] const SampleCache& getSamples() const noexcept { return instrumentSamples; }

    [[nodiscard]] std::optional<EffectLocation> findEffect(model::ObjectId effectId) const;
    [[nodiscard]] std::optional<model::Effect> getEffect(model::ObjectId effectId) const;
    /** The engine's instance (for the effect's description, its meters and whether it
        failed); null until the next update() after it was added. */
    [[nodiscard]] std::shared_ptr<engine::fx::EffectInstance>
    getEffectInstance(model::ObjectId effectId) const;
    [[nodiscard]] model::EffectList getEffects(const ChainOwner& owner) const;

    /** Why the last edit was refused, for the user; reading it clears it. */
    [[nodiscard]] juce::String takeEditProblem();

    // Plugins ------------------------------------------------------------------------------

    /** Adds a plugin of the catalogue (an effect, not an instrument) to an owner's
        chain at index (-1: the end); it plays once it has loaded. One undo step.
        Returns its id; 0 if refused (an instrument, the owner gone, recording, no
        plugins in this session). */
    model::ObjectId addPlugin(const ChainOwner& owner, const juce::PluginDescription& plugin, int index = -1);

    /** Gives a MIDI track a plugin instrument, replacing its instrument. One undo step;
        0 if refused. */
    model::ObjectId setInstrumentPlugin(model::ObjectId trackId, const juce::PluginDescription& plugin);

    [[nodiscard]] PluginStatus getPluginStatus(model::ObjectId effectId) const;
    /** Why a plugin is missing or failed. */
    [[nodiscard]] juce::String getPluginProblem(model::ObjectId effectId) const;

    /** The plugin a plugin effect plays through (its editor, its parameters' text);
        null while it loads, or if it is missing or failed. */
    [[nodiscard]] engine::plugins::PluginEffect* findPlugin(model::ObjectId effectId) const;

    /** The catalogue changed (a scan): plugins missing until now are looked for again. */
    void pluginsChanged();

    /** What each playing plugin takes of the real time since the last call. */
    struct PluginLoad
    {
        model::ObjectId effectId = 0;
        juce::String name;
        engine::plugins::PluginEffect::Load load;
    };
    [[nodiscard]] std::vector<PluginLoad> takePluginLoads();

    /** Fresh instances of the project's plugins for an export at this rate (message
        thread): loaded at once, given their current state, prepared offline. Plugins
        missing or failing to load are named in missing and left out. */
    struct ExportPlugins
    {
        std::map<model::ObjectId, std::shared_ptr<engine::fx::EffectInstance>> instances;
        juce::StringArray missing;
    };
    [[nodiscard]] ExportPlugins makeExportPlugins(double sampleRate) const;

    /** Adds an aux bus at the end; an empty name becomes "Bus n". Returns its id. */
    model::ObjectId addBus(const juce::String& name = {});
    /** With the sends to it. */
    void removeBus(model::ObjectId busId);
    void renameBus(model::ObjectId busId, const juce::String& name);
    void setBusVolume(model::ObjectId busId, double gain);
    void setBusPan(model::ObjectId busId, double pan);
    void setBusMuted(model::ObjectId busId, bool muted);
    /** Measured after its fader. */
    [[nodiscard]] std::shared_ptr<engine::dsp::PeakLevelMeter> getBusMeter(model::ObjectId busId);

    // Automation --------------------------------------------------------------------

    [[nodiscard]] model::Automation getAutomation(const ChainOwner& owner) const;
    /** Read or off. Not undoable. */
    void setAutomationMode(const ChainOwner& owner, model::AutomationMode mode);
    /** Adds a point to target's lane (made if needed); returns its index. */
    int addAutomationPoint(const ChainOwner& owner, const model::AutomationTarget& target,
                           model::AutomationPointData point);
    /** Changes a point; returns its index after moving among the others. A drag is
        one gesture. */
    int setAutomationPoint(const ChainOwner& owner, const model::AutomationTarget& target,
                           int index, model::AutomationPointData point);
    void removeAutomationPoint(const ChainOwner& owner, const model::AutomationTarget& target,
                               int index);
    /** Removes the lane with all its points. */
    void clearAutomation(const ChainOwner& owner, const model::AutomationTarget& target);

    /** The value (normalised) target's automation gives at the playhead; nothing if
        its owner's automation is off or it has no points. Controls show it, and
        cannot be moved while it is there. */
    [[nodiscard]] std::optional<double> getAutomatedValue(const ChainOwner& owner,
                                                          const model::AutomationTarget& target) const;

    /** A post-fader send at 0 dB from the track to the bus (or the one it has). */
    model::ObjectId addSend(model::ObjectId trackId, model::ObjectId busId);
    void removeSend(model::ObjectId trackId, model::ObjectId sendId);
    void setSendLevel(model::ObjectId trackId, model::ObjectId sendId, double gain);
    void setSendPreFader(model::ObjectId trackId, model::ObjectId sendId, bool preFader);

    /** What importing a MIDI file did. */
    struct MidiImport
    {
        juce::Result result = juce::Result::ok();
        std::vector<model::ObjectId> trackIds;
        juce::StringArray leftOut; ///< for the user: what the file had that was left out
    };

    /** Imports a standard MIDI file as one MIDI track per track and channel (channel
        10 with the drum sampler, the others with the synth), named after them, each
        with one clip from position to the bar after its end. A project without tracks
        first takes the file's tempo and metre. One undo step. */
    MidiImport importMidi(const juce::File& file, model::Ticks position);

    /** Writes the MIDI tracks to a standard MIDI file of type 0 or 1. */
    juce::Result exportMidi(const juce::File& file, int type) const;

    /** Adds audio files as clips starting at position: one file onto an existing
        track if ontoTrack is given, otherwise one new track per file (named after
        it). One undo step. Returns the new clips' ids. */
    std::vector<model::ObjectId> importAudio(const juce::Array<juce::File>& files,
                                             model::Ticks position,
                                             std::optional<model::ObjectId> ontoTrack = {});

    /** Onto another track of the same kind only. */
    void moveClip(model::ObjectId clipId, model::ObjectId toTrackId, model::Ticks position);
    void setClipBounds(model::ObjectId clipId, const model::ClipBounds& bounds);
    void deleteClips(const std::vector<model::ObjectId>& clipIds);

    /** Copies each clip right after itself on its track (at its end), on top. One
        undo step. Returns the copies' ids, in order. */
    std::vector<model::ObjectId> duplicateClips(const std::vector<model::ObjectId>& clipIds);

    /** Copies a clip to a track of the same kind at a position, on top (Alt+drag).
        Returns the copy's id; 0 if refused. */
    model::ObjectId copyClip(model::ObjectId clipId, model::ObjectId toTrackId,
                             model::Ticks position);

    // MIDI clips and notes ---------------------------------------------------------------

    /** An empty MIDI clip on a MIDI track, named after it. Returns its id; 0 if the
        track is not a MIDI track. */
    model::ObjectId addMidiClip(model::ObjectId trackId, model::Ticks position,
                                model::Ticks length);
    void renameClip(model::ObjectId clipId, const juce::String& name);

    /** A MIDI clip of the project, if clipId is one. */
    [[nodiscard]] std::optional<model::MidiClip> findMidiClip(model::ObjectId clipId) const;

    /** Replaces a MIDI clip's notes (see model::MidiClip::setNotes()): one undo step
        named actionName, or part of the gesture in progress. */
    void setNotes(model::ObjectId clipId, std::vector<model::Note> notes,
                  const juce::String& actionName);

    /** Adds a note; returns its id (0 if clipId is not a MIDI clip). */
    std::int32_t addNote(model::ObjectId clipId, model::Note note);
    void removeNotes(model::ObjectId clipId, const std::vector<std::int32_t>& noteIds);

    /** Replaces a MIDI clip's events of one controller type. */
    void setControllerEvents(model::ObjectId clipId, const model::ControllerType& type,
                             std::vector<model::ControllerEvent> events,
                             const juce::String& actionName);

    void setTempo(double quarterNotesPerMinute);
    void setTimeSignature(model::TimeSignature timeSignature);
    void setLoopRange(model::Ticks start, model::Ticks end);

    // Not undoable.
    void setLoopEnabled(bool shouldLoop);
    void setSnapEnabled(bool shouldSnap);
    void setGridDivision(model::GridDivision division);
    /** Arming fails (returns false) while input access is denied. */
    bool setTrackArmed(model::ObjectId trackId, bool armed);
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

    /** Records every armed track that has an input, and every armed MIDI track, from
        the playhead (after the count-in, if it is on; with punch on, keeping only the
        loop region), until stop(). The audio files go into the "Audio" folder next to
        the project, which must then have been saved; MIDI needs no file. Fails with a message for the user (unsaved project, nothing
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

    /** Whether the app may use the device's inputs (the microphone permission). */
    enum class InputAccess
    {
        unknown, ///< not asked yet: the first need for an input asks
        granted,
        denied
    };
    void setInputAccess(InputAccess access) noexcept { inputAccess = access; }
    [[nodiscard]] InputAccess getInputAccess() const noexcept { return inputAccess; }

    /** Whether the inputs are needed now: a track armed or monitoring always, or
        someone asked (requestInputs(), e.g. the input settings being open). */
    [[nodiscard]] bool needsInputs() const;
    void requestInputs() noexcept { inputsRequested = true; }

    /** The MIDI input devices connected now, by name, and the index the engine knows
        each by (set by the app when they change): a MIDI track listening to one
        device finds it here; one that is not connected hears nothing. */
    void setMidiInputDevices(juce::StringArray names, MidiDeviceLookup lookup);
    [[nodiscard]] const juce::StringArray& getMidiInputDeviceNames() const noexcept
    {
        return midiInputNames;
    }

    /** Plays a note on a MIDI track through its instrument (the piano roll's preview,
        whatever the monitoring) until stopNotePreview(), stop or pause; a track that
        is not heard plays none. velocity 1 to 127. False if not played (not a MIDI
        track, or too many previews at once). Views use a NotePreview. */
    bool startNotePreview(model::ObjectId trackId, int key, int velocity);
    /** Ends a preview note. A note-off the engine cannot take now is sent again at
        the next update(): it is never lost. */
    void stopNotePreview(model::ObjectId trackId, int key);

    /** Panic: every instrument (built-in or plugin) stops every voice at once and
        forgets the notes it holds; the transport goes on (see Engine::panic()). */
    void panic();

    /** The device's input channels by number, for choosing a track's input (set by
        the app when the device changes; empty while no input is open). */
    void setInputChannelNames(juce::StringArray names) { inputChannelNames = std::move(names); }
    [[nodiscard]] const juce::StringArray& getInputChannelNames() const noexcept
    {
        return inputChannelNames;
    }

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
    [[nodiscard]] int getDeviceInputLatency() const noexcept { return deviceInputLatency; }
    [[nodiscard]] int getDeviceOutputLatency() const noexcept { return deviceOutputLatency; }
    [[nodiscard]] juce::int64 getRecordingLatency() const noexcept;

    /** Measures the round trip with a cable from output outputChannel (0 or 1) to
        input inputChannel; stops playback first. Not while recording. The result
        comes from takeLatencyResult() after a few seconds of update()s; applying
        its offset is up to the caller. */
    juce::Result measureLatency(int outputChannel, int inputChannel);
    [[nodiscard]] bool isMeasuringLatency() const noexcept
    {
        return latencyMeasurement.isRunning();
    }
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
    void sendPreviewOffs();
    void takePluginChanges();
    void storePluginStates();
    void keepPluginState(const model::Effect& effect);
    void keepPluginStates(const model::EffectList& effects);
    void pushToPlugin(const juce::ValueTree& tree, const juce::Identifier& property);
    void replaceProject(model::Project newProject);
    void addRecording(const RecordingOutcome& outcome);
    void copyRecordingsTo(const juce::File& newProjectDirectory);
    void writeJournal();
    void leaveProject();
    void requestAllSources();
    void sourceChanged(const juce::File& file);
    void resolveLengths(const juce::File& file);
    void rebuildArrangement();
    [[nodiscard]] std::optional<model::Bus> findBus(model::ObjectId busId) const;
    [[nodiscard]] bool refusesLatencyChange(int latency);
    [[nodiscard]] bool isInstrumentType(const juce::String& type) const;
    [[nodiscard]] engine::midi::InputFilter midiFilterOf(const model::Track& track) const;
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
    SampleCache instrumentSamples{fallbackSampleRate};
    std::unique_ptr<engine::Arrangement> pendingArrangement; // waits for its audio
    std::unique_ptr<engine::Arrangement> playingArrangement; // a copy of the engine's, for the plan
    juce::uint32 pendingSince = 0;
    std::uint64_t dropoutsSeen = 0;
    std::atomic<juce::int64> seekCue{-1}; // the last seek, for the read-ahead
    juce::UndoManager undoManager{10'000'000, 200};
    model::Project project;
    juce::File projectFile;
    std::map<model::ObjectId, std::shared_ptr<engine::dsp::PeakLevelMeter>> meters;
    EffectInstances effectInstances;
    std::unique_ptr<PluginInstances> pluginInstances; // with plugins only
    std::vector<engine::plugins::PluginEffect::ParameterReport> pluginReports;
    struct PluginEdit
    {
        model::ObjectId effectId = 0;
        int index = -1;
        juce::uint32 lastChange = 0;
        bool gesture = false;
    } pluginEdit; // the plugin edit the next change may belong to
    bool writingFromPlugin = false;
    FadeOutKeeper fadeOutKeeper;
    bool freshEffects = true; // the next arrangement's new effects start at once
    juce::String editProblem;
    std::array<float, engine::AudioInputs::maxChannels> inputPeaks{};

    RecordingController recording{engine};
    juce::String recordingProblem;
    std::vector<JournalEntry> journal;     // this project's recordings not saved yet
    std::vector<JournalEntry> recoverable; // found on opening it
    LatencyMeasurement latencyMeasurement{engine};
    std::optional<LatencyMeasurement::Result> latencyResult;
    InputAccess inputAccess = InputAccess::unknown;
    bool inputsRequested = false;
    juce::StringArray inputChannelNames;
    juce::StringArray midiInputNames;
    std::vector<std::pair<model::ObjectId, int>> unsentPreviewOffs; // queue was full
    MidiDeviceLookup midiDevices;
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
