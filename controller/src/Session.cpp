#include "daw/controller/Session.h"

#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/TempoBeatGrid.h"
#include "daw/controller/TimelineViewport.h"
#include "daw/engine/io/WavRepair.h"
#include "daw/model/ProjectFile.h"

#include <algorithm>
#include <set>
#include <utility>

namespace daw::controller
{

Session::Session(engine::Engine& engineToDrive, SessionOptions sessionOptions)
    : engine(engineToDrive)
    , options(sessionOptions)
    , sources(fallbackSampleRate, 2,
              engine::io::StreamingOptions{options.streaming, &engine.getRenderClock(),
                                           options.streamingMemory, true})
{
    if (engine.getSampleRate() > 0.0)
        sources.setSampleRate(engine.getSampleRate());

    // The read-ahead follows the engine's playhead (atomics: safe from its thread).
    sources.followPlayhead([&playing = engine, &cue = seekCue] {
        engine::io::DiskStreamer::Playhead playhead;
        playhead.cue = cue.load();
        playhead.position = playing.getPlayheadPosition();
        playhead.playing = playing.getTransportState() == engine::TransportState::playing;
        playhead.looping = playing.isLooping();
        playhead.loopStart = playing.getLoopStart();
        playhead.loopEnd = playing.getLoopEnd();
        return playhead;
    });

    sources.onSourceChanged = [this](const juce::File& file) { sourceChanged(file); };
    project.getState().addListener(this);
}

Session::~Session()
{
    project.getState().removeListener(this);
    sources.onSourceChanged = nullptr;
}

void Session::update()
{
    if (const auto outcome = recording.update())
        addRecording(*outcome);

    if (auto result = latencyMeasurement.update())
        latencyResult = std::move(result);

    sources.collectFinishedLoads();

    if (arrangementOutOfDate)
        rebuildArrangement();

    streamingTick();
    engine.collectGarbage();
}

void Session::streamingTick()
{
    auto& streamer = sources.getStreamer();
    if (options.streaming == engine::io::DiskStreamer::Mode::manual)
        streamer.service();

    // A new arrangement waiting for its audio goes to the engine once that is loaded,
    // once playback stops, or when it has waited long enough.
    if (pendingArrangement != nullptr)
    {
        const bool playing = engine.getTransportState() == engine::TransportState::playing;
        const auto waited = juce::Time::getMillisecondCounter() - pendingSince;
        if (!playing ||
            engine::isArrangementReady(*pendingArrangement, engine.getPlayheadPosition(),
                                       static_cast<juce::int64>(engine::Engine::readyAheadSeconds *
                                                                pendingArrangement->sampleRate)) ||
            waited >= static_cast<juce::uint32>(maximumArrangementWait * 1000.0))
            handOver(std::move(pendingArrangement));
    }
}

bool Session::waitForLoads(int timeoutMilliseconds)
{
    const bool idle = sources.waitForLoads(timeoutMilliseconds);
    update();
    return idle;
}

std::uint64_t Session::takeDropouts()
{
    const auto total = sources.getStreamer().getDropouts();
    const auto newOnes = total >= dropoutsSeen ? total - dropoutsSeen : total;
    dropoutsSeen = total;
    return newOnes;
}

void Session::setSampleRate(double sampleRate)
{
    if (!(sampleRate > 0.0) || juce::exactlyEqual(sampleRate, sources.getSampleRate()))
        return;

    sources.setSampleRate(sampleRate);
    dropoutsSeen = 0;
    pendingArrangement.reset();
    arrangementOutOfDate = true;
}

// Project files ----------------------------------------------------------------------

void Session::newProject()
{
    leaveProject();
    replaceProject(model::Project());
    projectFile = juce::File();
}

juce::Result Session::openProject(const juce::File& file)
{
    auto loaded = model::loadProject(file);
    if (!loaded.succeeded())
        return juce::Result::fail(loaded.error);

    leaveProject();
    replaceProject(std::move(*loaded.project));
    projectFile = file;

    // Recordings in the journal whose files exist and that no clip uses.
    std::set<juce::String> used;
    for (int t = 0; t < project.getNumTracks(); ++t)
        for (int c = 0; c < project.getTrack(t).getNumClips(); ++c)
            used.insert(project.getTrack(t).getClip(c).getAudioFile().getFullPathName());

    for (const auto& entry : RecordingJournal::read(file.getParentDirectory()))
        if (entry.file.existsAsFile() && !used.contains(entry.file.getFullPathName()))
            recoverable.push_back(entry);

    journal = recoverable;
    writeJournal();
    return juce::Result::ok();
}

void Session::leaveProject()
{
    // Leaving on purpose (the UI asked about unsaved changes first): its unsaved
    // recordings are not to be offered again.
    if (projectFile != juce::File())
        RecordingJournal::write(projectFile.getParentDirectory(), {});

    journal.clear();
    recoverable.clear();
}

void Session::writeJournal()
{
    if (projectFile != juce::File())
        RecordingJournal::write(projectFile.getParentDirectory(), journal);
}

juce::Result Session::recoverRecordings()
{
    juce::StringArray failed;
    bool started = false;

    for (const auto& entry : std::exchange(recoverable, {}))
    {
        const auto repaired = engine::io::repairWav(entry.file);
        if (repaired.result.failed() || repaired.numFrames <= 0)
        {
            failed.add(entry.file.getFileName());
            continue;
        }

        const auto fileEnd =
            entry.firstPosition + model::samplesToTicks(repaired.numFrames, repaired.sampleRate);
        const auto start = std::max({entry.keepStart, entry.firstPosition, model::Ticks{0}});
        const auto end = std::min(entry.keepEnd.value_or(fileEnd), fileEnd);
        if (end <= start)
            continue;

        if (!started)
            startEdit("Recover Recordings");
        started = true;

        auto track = findTrack(entry.trackId);
        if (!track)
            track = project.addTrack(entry.file.getFileNameWithoutExtension(), &undoManager);

        sources.reload(entry.file);
        track->addClip(entry.file, {start, start - entry.firstPosition, end - start}, &undoManager);
    }

    if (failed.isEmpty())
        return juce::Result::ok();

    return juce::Result::fail("These recordings could not be recovered: " +
                              failed.joinIntoString(", ") + ".");
}

void Session::discardRecoverableRecordings()
{
    for (const auto& entry : std::exchange(recoverable, {}))
    {
        entry.file.deleteFile();
        std::erase(journal, entry);
    }

    writeJournal();
}

juce::Result Session::saveProject(const juce::File& file)
{
    const auto target = file.withFileExtension(model::projectFileExtension);

    // Saved somewhere else, the project takes its recordings along.
    if (projectFile != juce::File() &&
        target.getParentDirectory() != projectFile.getParentDirectory())
        copyRecordingsTo(target.getParentDirectory());

    const auto result = model::saveProject(project, target);

    if (result.wasOk())
    {
        // Everything recorded is in the saved file now (or was undone on purpose).
        if (projectFile != juce::File())
            RecordingJournal::write(projectFile.getParentDirectory(), {});
        journal.clear();
        recoverable.clear();
        projectFile = target;
        unsavedChanges = false;
    }

    return result;
}

void Session::copyRecordingsTo(const juce::File& newProjectDirectory)
{
    const auto oldFolder = projectFile.getParentDirectory().getChildFile(recordingsFolderName);
    const auto newFolder = newProjectDirectory.getChildFile(recordingsFolderName);
    std::map<juce::String, juce::File> copies; // by original path: each file copied once

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        for (int c = 0; c < track.getNumClips(); ++c)
        {
            auto clip = track.getClip(c);
            const auto original = clip.getAudioFile();
            if (!original.isAChildOf(oldFolder) || !original.existsAsFile())
                continue;

            auto& copy = copies[original.getFullPathName()];
            if (copy == juce::File())
            {
                auto destination = newFolder.getChildFile(original.getRelativePathFrom(oldFolder));
                if (destination.exists() && !destination.hasIdenticalContentTo(original))
                    destination = destination.getNonexistentSibling();

                if (!destination.getParentDirectory().createDirectory() ||
                    (!destination.exists() && !original.copyFileTo(destination)))
                    continue; // left where it is: saved as an absolute path
                copy = destination;
            }

            clip.setAudioFile(copy);
            sources.request(copy);
        }
    }
}

void Session::replaceProject(model::Project newProject)
{
    jassert(!recording.isActive()); // the UI stops a recording before replacing the project

    project.getState().removeListener(this);
    project = std::move(newProject);
    project.getState().addListener(this);

    undoManager.clearUndoHistory();
    inGesture = false;
    unsavedChanges = false;
    sources.clear();
    meters.clear();

    engine.resetTransport();
    engine.setLooping(project.isLoopEnabled());

    requestAllSources();
    arrangementOutOfDate = true;
    ++revision;
}

// Reading ----------------------------------------------------------------------------

const engine::io::PooledSource* Session::findSource(const juce::File& file) const
{
    return sources.find(file);
}

std::optional<model::Ticks> Session::getFileLength(const juce::File& file) const
{
    const auto* source = sources.find(file);
    if (source == nullptr || source->audio == nullptr)
        return std::nullopt;

    return model::samplesToTicks(source->audio->getLength(), sources.getSampleRate());
}

std::shared_ptr<engine::dsp::PeakLevelMeter> Session::getTrackMeter(model::ObjectId trackId)
{
    auto& meter = meters[trackId];
    if (meter == nullptr)
        meter = std::make_shared<engine::dsp::PeakLevelMeter>();
    return meter;
}

void Session::takeInputPeaks()
{
    for (int channel = 0; channel < engine::AudioInputs::maxChannels; ++channel)
        inputPeaks[static_cast<std::size_t>(channel)] = engine.takeInputPeak(channel);
}

float Session::getInputPeak(int channel) const noexcept
{
    if (channel < 0 || channel >= engine::AudioInputs::maxChannels)
        return 0.0f;

    return inputPeaks[static_cast<std::size_t>(channel)];
}

std::array<float, 2> Session::getTrackInputPeaks(model::ObjectId trackId) const
{
    const auto track = findTrack(trackId);
    if (!track || track->getInput().isNone())
        return {0.0f, 0.0f};

    const auto input = track->getInput();
    const float left = getInputPeak(input.channel);
    return {left, input.stereo ? getInputPeak(input.channel + 1) : left};
}

Snapper Session::makeSnapper(double ticksPerPixel, bool bypass) const
{
    if (bypass || !project.isSnapEnabled())
        return {};

    const auto tempoMap = project.getTempoMap();
    auto division = project.getGridDivision();
    if (division == model::GridDivision::automatic)
        division = model::Grid::automaticDivision(tempoMap, ticksPerPixel,
                                                  TimelineViewport::minimumGridSpacingPixels);

    return Snapper(model::Grid(tempoMap, division));
}

// Edits ------------------------------------------------------------------------------

void Session::beginGesture(const juce::String& name)
{
    undoManager.beginNewTransaction(name);
    inGesture = true;
}

void Session::endGesture()
{
    inGesture = false;
}

void Session::startEdit(const juce::String& name)
{
    if (!inGesture)
        undoManager.beginNewTransaction(name);
}

model::ObjectId Session::addTrack(const juce::String& name)
{
    startEdit("Add Track");
    const auto trackName =
        name.isNotEmpty() ? name : "Track " + juce::String(project.getNumTracks() + 1);
    return project.addTrack(trackName, &undoManager).getId();
}

void Session::removeTrack(model::ObjectId trackId)
{
    if (const int index = project.indexOfTrack(trackId); index >= 0)
    {
        startEdit("Delete Track");
        project.removeTrack(index, &undoManager);
    }
}

void Session::renameTrack(model::ObjectId trackId, const juce::String& name)
{
    if (auto track = findTrack(trackId))
    {
        startEdit("Rename Track");
        track->setName(name, &undoManager);
    }
}

void Session::setTrackVolume(model::ObjectId trackId, double gain)
{
    if (auto track = findTrack(trackId))
    {
        startEdit("Volume");
        track->setVolume(gain, &undoManager);
    }
}

void Session::setTrackPan(model::ObjectId trackId, double pan)
{
    if (auto track = findTrack(trackId))
    {
        startEdit("Pan");
        track->setPan(pan, &undoManager);
    }
}

void Session::setTrackMuted(model::ObjectId trackId, bool muted)
{
    if (auto track = findTrack(trackId))
    {
        startEdit(muted ? "Mute" : "Unmute");
        track->setMuted(muted, &undoManager);
    }
}

void Session::setTrackSoloed(model::ObjectId trackId, bool soloed)
{
    if (auto track = findTrack(trackId))
    {
        startEdit(soloed ? "Solo" : "Unsolo");
        track->setSoloed(soloed, &undoManager);
    }
}

void Session::setTrackInput(model::ObjectId trackId, model::TrackInput input)
{
    if (auto track = findTrack(trackId); track && track->getInput() != input.normalised())
    {
        startEdit("Track Input");
        track->setInput(input, &undoManager);
    }
}

void Session::setTrackMonitoring(model::ObjectId trackId, model::MonitoringMode mode)
{
    if (auto track = findTrack(trackId); track && track->getMonitoring() != mode)
    {
        startEdit("Input Monitoring");
        track->setMonitoring(mode, &undoManager);
    }
}

void Session::setMasterVolume(double gain)
{
    startEdit("Master Volume");
    project.setMasterVolume(gain, &undoManager);
}

std::vector<model::ObjectId> Session::importAudio(const juce::Array<juce::File>& files,
                                                  model::Ticks position,
                                                  std::optional<model::ObjectId> ontoTrack)
{
    std::vector<model::ObjectId> clipIds;
    if (files.isEmpty())
        return clipIds;

    startEdit(files.size() == 1 ? "Import Audio File" : "Import Audio Files");
    const bool onExistingTrack =
        files.size() == 1 && ontoTrack && project.indexOfTrack(*ontoTrack) >= 0;

    for (const auto& file : files)
    {
        sources.request(file);

        auto track = onExistingTrack
                         ? project.getTrack(project.indexOfTrack(*ontoTrack))
                         : project.addTrack(file.getFileNameWithoutExtension(), &undoManager);

        model::ClipBounds bounds{std::max(model::Ticks{0}, position), 0, getFileLength(file)};
        clipIds.push_back(track.addClip(file, bounds, &undoManager).getId());
    }

    return clipIds;
}

void Session::moveClip(model::ObjectId clipId, model::ObjectId toTrackId, model::Ticks position)
{
    const int trackIndex = project.indexOfTrack(toTrackId);
    const auto location = project.findClip(clipId);
    if (trackIndex < 0 || !location)
        return;

    const auto track = project.getTrack(location->trackIndex);
    const bool onTop = location->clipIndex == track.getNumClips() - 1;
    if (location->trackIndex == trackIndex && onTop &&
        track.getClip(location->clipIndex).getPosition() == position)
        return; // nothing would change

    startEdit("Move Clip");
    project.moveClip(clipId, trackIndex, position, &undoManager);
}

void Session::setClipBounds(model::ObjectId clipId, const model::ClipBounds& bounds)
{
    if (const auto location = project.findClip(clipId))
    {
        startEdit("Trim Clip");
        project.getTrack(location->trackIndex)
            .getClip(location->clipIndex)
            .setBounds(bounds, &undoManager);
    }
}

void Session::deleteClips(const std::vector<model::ObjectId>& clipIds)
{
    bool started = false;
    for (const auto clipId : clipIds)
    {
        if (!project.findClip(clipId))
            continue;

        if (!started)
            startEdit(clipIds.size() == 1 ? "Delete Clip" : "Delete Clips");
        started = true;
        project.removeClip(clipId, &undoManager);
    }
}

void Session::setTempo(double quarterNotesPerMinute)
{
    startEdit("Tempo");
    project.setTempo(quarterNotesPerMinute, &undoManager);
}

void Session::setTimeSignature(model::TimeSignature timeSignature)
{
    startEdit("Time Signature");
    project.setTimeSignature(timeSignature, &undoManager);
}

void Session::setLoopRange(model::Ticks start, model::Ticks end)
{
    startEdit("Loop Region");
    project.setLoopRange(start, end, &undoManager);
}

void Session::setLoopEnabled(bool shouldLoop)
{
    project.setLoopEnabled(shouldLoop);
    engine.setLooping(shouldLoop);
}

void Session::setSnapEnabled(bool shouldSnap)
{
    project.setSnapEnabled(shouldSnap);
}

void Session::setGridDivision(model::GridDivision division)
{
    project.setGridDivision(division);
}

bool Session::setTrackArmed(model::ObjectId trackId, bool armed)
{
    if (armed && inputAccess == InputAccess::denied)
        return false;

    if (auto track = findTrack(trackId))
    {
        track->setArmed(armed);
        return true;
    }
    return false;
}

bool Session::needsInputs() const
{
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        if (!track.getInput().isNone() &&
            (track.isArmed() || track.getMonitoring() == model::MonitoringMode::on))
            return true;
    }
    return false;
}

void Session::setMetronomeEnabled(bool shouldClick)
{
    project.setMetronomeEnabled(shouldClick);
}

void Session::setMetronomeVolume(double gain)
{
    project.setMetronomeVolume(gain);
}

void Session::setCountInEnabled(bool shouldCountIn)
{
    project.setCountInEnabled(shouldCountIn);
}

void Session::setCountInBars(int bars)
{
    project.setCountInBars(bars);
}

void Session::setPunchEnabled(bool shouldPunch)
{
    project.setPunchEnabled(shouldPunch);
}

void Session::setRecordingBitsPerSample(int bitsPerSample)
{
    project.setRecordingBitsPerSample(bitsPerSample);
}

juce::String Session::getUndoDescription() const
{
    return undoManager.getUndoDescription();
}

juce::String Session::getRedoDescription() const
{
    return undoManager.getRedoDescription();
}

bool Session::undo()
{
    inGesture = false;
    return undoManager.undo();
}

bool Session::redo()
{
    inGesture = false;
    return undoManager.redo();
}

// Transport --------------------------------------------------------------------------

void Session::play()
{
    engine.play();
}

void Session::pause()
{
    if (recording.isActive())
        recording.stop();
    else
        engine.pause();
}

void Session::stop()
{
    if (recording.isActive())
        recording.stop();
    else
        engine.stop();
}

// Recording --------------------------------------------------------------------------

juce::Result Session::record()
{
    if (recording.isActive())
        return juce::Result::fail("A recording is already running.");
    if (latencyMeasurement.isRunning())
        return juce::Result::fail("Wait for the latency measurement to finish.");
    if (inputAccess == InputAccess::denied)
        return juce::Result::fail(microphoneDeniedMessage);
    if (projectFile == juce::File())
        return juce::Result::fail("Save the project first: recordings go into its folder.");

    const double rate = engineRate();
    RecordingRequest request;
    request.folder = projectFile.getParentDirectory().getChildFile(recordingsFolderName);
    request.sampleRate = rate;
    request.bitsPerSample = project.getRecordingBitsPerSample();
    request.latency = getRecordingLatency();

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        const auto input = track.getInput();
        if (track.isArmed() && !input.isNone())
            request.tracks.push_back(
                {track.getId(), track.getName(), input.channel, input.getNumChannels()});
    }

    const auto start = getPlayhead();
    request.startPosition = model::ticksToSamples(start, rate);

    if (project.isCountInEnabled())
        request.countInLength =
            request.startPosition -
            model::ticksToSamples(countInStart(project.getTempoMap(), start,
                                               project.getCountInBars()),
                                  rate);

    if (project.isPunchEnabled())
    {
        if (project.getLoopEnd() <= project.getLoopStart())
            return juce::Result::fail("Punch in/out records the loop region: set one first.");
        request.punchIn = model::ticksToSamples(project.getLoopStart(), rate);
        request.punchOut = model::ticksToSamples(project.getLoopEnd(), rate);
    }

    // Recording starts from a silent transport.
    if (engine.getTransportState() != engine::TransportState::stopped)
        engine.stop();

    recordingProblem.clear();
    if (const auto started = recording.start(request); started.failed())
        return started;

    // Written down before the first sample, so that a crash leaves it recoverable.
    const auto keepStart = model::samplesToTicks(
        request.punchOut > request.punchIn ? request.punchIn : request.startPosition, rate);
    for (const auto& take : recording.getLiveTakes())
    {
        JournalEntry entry{take.file, take.trackId, model::samplesToTicks(take.firstPosition, rate),
                           keepStart, std::nullopt};
        if (request.punchOut > request.punchIn)
            entry.keepEnd = model::samplesToTicks(request.punchOut, rate);
        journal.push_back(entry);
    }
    writeJournal();
    return juce::Result::ok();
}

void Session::discardRecording()
{
    recording.discard();
}

juce::String Session::takeRecordingProblem()
{
    return std::exchange(recordingProblem, {});
}

void Session::setDeviceLatency(int inputSamples, int outputSamples)
{
    deviceInputLatency = std::max(0, inputSamples);
    deviceOutputLatency = std::max(0, outputSamples);
}

void Session::setLatencyOffset(int samples)
{
    latencyOffset = samples;
}

juce::int64 Session::getRecordingLatency() const noexcept
{
    return std::max(juce::int64{0},
                    juce::int64{deviceInputLatency} + deviceOutputLatency + latencyOffset);
}

juce::Result Session::measureLatency(int outputChannel, int inputChannel)
{
    if (recording.isActive())
        return juce::Result::fail("Stop recording before measuring the latency.");
    if (inputAccess == InputAccess::denied)
        return juce::Result::fail(microphoneDeniedMessage);

    latencyResult.reset();
    return latencyMeasurement.start(outputChannel, inputChannel,
                                    juce::int64{deviceInputLatency} + deviceOutputLatency);
}

std::optional<LatencyMeasurement::Result> Session::takeLatencyResult()
{
    return std::exchange(latencyResult, std::nullopt);
}

void Session::addRecording(const RecordingOutcome& outcome)
{
    if (outcome.problem.isNotEmpty())
        recordingProblem = outcome.problem;

    const double rate = engineRate();
    bool started = false;

    for (const auto& take : outcome.takes)
    {
        const auto start = std::max({outcome.keepStart, take.firstPosition, juce::int64{0}});
        const auto end = std::min(outcome.keepEnd, take.firstPosition + take.length);
        auto track = findTrack(take.trackId);

        // Nothing to keep (stopped during the count-in, before the punch in, or the
        // track was deleted meanwhile): the file goes too.
        if (end <= start || !track)
        {
            take.file.deleteFile();
            continue;
        }

        if (!started)
            startEdit("Record");
        started = true;

        const auto position = model::samplesToTicks(start, rate);
        const auto offset = model::samplesToTicks(start - take.firstPosition, rate);
        const auto length = model::samplesToTicks(end, rate) - position;
        sources.request(take.file);
        track->addClip(take.file, {position, offset, length}, &undoManager);

        for (auto& entry : journal)
            if (entry.file == take.file)
            {
                entry.keepStart = position;
                entry.keepEnd = position + length;
            }
    }

    // Takes that were discarded or kept nothing are gone from the disk.
    std::erase_if(journal, [](const JournalEntry& entry) { return !entry.file.existsAsFile(); });
    writeJournal();
}

void Session::seek(model::Ticks position)
{
    const auto target = model::ticksToSamples(std::max(model::Ticks{0}, position), engineRate());
    seekCue.store(target); // the read-ahead starts loading there at once
    engine.seek(target);
}

model::Ticks Session::getPlayhead() const
{
    return model::samplesToTicks(engine.getPlayheadPosition(), engineRate());
}

engine::TransportState Session::getTransportState() const
{
    return engine.getTransportState();
}

// Internals --------------------------------------------------------------------------

void Session::requestAllSources()
{
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        for (int c = 0; c < track.getNumClips(); ++c)
            sources.request(track.getClip(c).getAudioFile());
    }
}

void Session::sourceChanged(const juce::File& file)
{
    resolveLengths(file);
    arrangementOutOfDate = true;
    ++revision;
}

void Session::resolveLengths(const juce::File& file)
{
    const auto length = getFileLength(file);
    if (!length)
        return;

    // Filling in a length learnt from the file is not an edit by the user.
    const juce::ScopedValueSetter<bool> resolving(resolvingLengths, true);

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        for (int c = 0; c < track.getNumClips(); ++c)
        {
            auto clip = track.getClip(c);
            if (!clip.getLength() && clip.getAudioFile() == file)
                clip.resolveLength(*length);
        }
    }
}

void Session::rebuildArrangement()
{
    arrangementOutOfDate = false;

    const auto lookup = [this](const juce::File& file) {
        const auto* source = sources.find(file);
        return source != nullptr ? source->audio : nullptr;
    };
    const auto meterFor = [this](model::ObjectId trackId) { return getTrackMeter(trackId); };

    auto arrangement = buildArrangement(project, sources.getSampleRate(), lookup, meterFor);

    // While playing, what the new arrangement plays at the playhead must be loaded
    // before it takes over; meanwhile the read-ahead serves both.
    const bool playing = engine.getTransportState() == engine::TransportState::playing;
    if (playing && playingArrangement != nullptr &&
        !engine::isArrangementReady(*arrangement, engine.getPlayheadPosition(),
                                    static_cast<juce::int64>(engine::Engine::readyAheadSeconds *
                                                             arrangement->sampleRate)))
    {
        if (pendingArrangement == nullptr)
            pendingSince = juce::Time::getMillisecondCounter();
        pendingArrangement = std::move(arrangement);
        sources.getStreamer().setPlan({playingArrangement.get(), pendingArrangement.get()});
        return;
    }

    pendingArrangement.reset();
    handOver(std::move(arrangement));
}

void Session::handOver(std::unique_ptr<engine::Arrangement> arrangement)
{
    sources.getStreamer().setPlan(*arrangement);
    playingArrangement = std::make_unique<engine::Arrangement>(*arrangement);
    engine.setArrangement(std::move(arrangement));
}

double Session::engineRate() const noexcept
{
    return engine.getSampleRate() > 0.0 ? engine.getSampleRate() : sources.getSampleRate();
}

std::optional<model::Track> Session::findTrack(model::ObjectId trackId) const
{
    const int index = project.indexOfTrack(trackId);
    if (index < 0)
        return std::nullopt;
    return project.getTrack(index);
}

void Session::projectChanged()
{
    arrangementOutOfDate = true;
    ++revision;
    if (!resolvingLengths)
        unsavedChanges = true;
}

void Session::valueTreePropertyChanged(juce::ValueTree&, const juce::Identifier&)
{
    projectChanged();
}

void Session::valueTreeChildAdded(juce::ValueTree&, juce::ValueTree&)
{
    // A clip or track added (or brought back by undo) may use a file not loaded yet.
    requestAllSources();
    projectChanged();
}

void Session::valueTreeChildRemoved(juce::ValueTree&, juce::ValueTree&, int)
{
    projectChanged();
}

void Session::valueTreeChildOrderChanged(juce::ValueTree&, int, int)
{
    projectChanged();
}

} // namespace daw::controller
