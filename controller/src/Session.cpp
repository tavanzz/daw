#include "daw/controller/Session.h"

#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/AutomationEditing.h"
#include "daw/controller/MidiFiles.h"
#include "daw/controller/MidiTakes.h"
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
    effectInstances.setFactory(options.effects);
    if (options.plugins != nullptr && options.catalogue != nullptr)
    {
        pluginInstances = std::make_unique<PluginInstances>(*options.plugins, *options.catalogue);
        effectInstances.setPlugins(pluginInstances.get());
    }
    if (engine.getSampleRate() > 0.0)
    {
        sources.setSampleRate(engine.getSampleRate());
        instrumentSamples.setSampleRate(engine.getSampleRate());
    }

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
    effectInstances.prepare(sources.getSampleRate(), effectBlockSize);
}

Session::~Session()
{
    project.getState().removeListener(this);
    sources.onSourceChanged = nullptr;
}

void Session::update()
{
    sendPreviewOffs();

    if (pluginInstances != nullptr)
    {
        if (pluginInstances->update())
            arrangementOutOfDate = true;
        takePluginChanges();
        takePluginLoads();
    }

    if (const auto outcome = recording.update())
        addRecording(*outcome);

    if (auto result = latencyMeasurement.update())
        latencyResult = std::move(result);

    sources.collectFinishedLoads();
    if (instrumentSamples.collectFinished())
    {
        arrangementOutOfDate = true;
        ++revision;
    }

    // Also when something removed has faded out long enough to go.
    if (arrangementOutOfDate ||
        fadeOutKeeper.isDue(juce::Time::getMillisecondCounterHiRes() / 1000.0))
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
    const bool idle = sources.waitForLoads(timeoutMilliseconds) &&
                      instrumentSamples.waitForLoads(timeoutMilliseconds);
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
    instrumentSamples.setSampleRate(sampleRate);
    effectInstances.prepare(sampleRate, effectBlockSize);
    freshEffects = true;
    fadeOutKeeper.clear();
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

juce::Result Session::openProject(const juce::File& file, bool withPlugins)
{
    auto loaded = model::loadProject(file);
    if (!loaded.succeeded())
        return juce::Result::fail(loaded.error);
    if (pluginInstances != nullptr)
        pluginInstances->holdBackAll(!withPlugins);

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
    const auto target = model::withProjectFileExtension(file);
    storePluginStates();

    // Saved somewhere else, the project takes its recordings along.
    if (projectFile != juce::File() &&
        target.getParentDirectory() != projectFile.getParentDirectory())
        copyRecordingsTo(target.getParentDirectory());

    const auto result = model::saveProject(project, target);

    if (result.wasOk())
    {
        // Everything recorded is in the saved file now (or was undone on purpose),
        // except a recording still in progress: it stays recoverable.
        if (projectFile != juce::File())
            RecordingJournal::write(projectFile.getParentDirectory(), {});
        const auto takes = recording.getLiveTakes();
        std::erase_if(journal, [&takes](const JournalEntry& entry) {
            return std::none_of(takes.begin(), takes.end(),
                                [&entry](const auto& take) { return take.file == entry.file; });
        });
        recoverable.clear();
        projectFile = target;
        unsavedChanges = false;
        writeJournal();
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
    instrumentSamples.clear();
    meters.clear();
    effectInstances.clear();
    fadeOutKeeper.clear();
    freshEffects = true;

    engine.resetTransport();
    engine.setLooping(project.isLoopEnabled());

    requestAllSources();
    instrumentSamples.requestAll(project);
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

model::ObjectId Session::addMidiTrack(const juce::String& name, const juce::String& instrumentType)
{
    startEdit("Add MIDI Track");
    const auto trackName =
        name.isNotEmpty() ? name : "MIDI " + juce::String(project.getNumTracks() + 1);
    auto track = project.addTrack(trackName, model::TrackKind::midi, &undoManager);
    if (isInstrumentType(instrumentType))
        track.setInstrument(instrumentType, {}, &undoManager);
    return track.getId();
}

void Session::setTrackMidiInput(model::ObjectId trackId, const model::MidiInput& input)
{
    if (auto track = findTrack(trackId); track && track->isMidi() && track->getMidiInput() != input)
    {
        startEdit("MIDI Input");
        track->setMidiInput(input, &undoManager);
    }
}

void Session::setTrackMidiChannel(model::ObjectId trackId, int channel)
{
    if (auto track = findTrack(trackId); track && track->isMidi() && track->getMidiChannel() != channel)
    {
        startEdit("MIDI Channel");
        track->setMidiChannel(channel, &undoManager);
    }
}

bool Session::isInstrumentType(const juce::String& type) const
{
    if (type.isEmpty())
        return false;
    const auto probe = EffectInstances::makeInstance(type, sources.getSampleRate(), effectBlockSize,
                                                     false, effectInstances.getFactory());
    return probe != nullptr && probe->getDescription().isInstrument;
}

model::ObjectId Session::setInstrument(model::ObjectId trackId, const juce::String& type)
{
    auto track = findTrack(trackId);
    if (!track || !track->isMidi() || !isInstrumentType(type))
        return 0;

    const auto probe = EffectInstances::makeInstance(type, sources.getSampleRate(), effectBlockSize,
                                                     false, effectInstances.getFactory());
    const auto old = track->getInstrument();
    const auto oldInstance = old ? effectInstances.find(old->getId()) : nullptr;
    if (refusesLatencyChange(std::max(probe->getLatency(),
                                      oldInstance != nullptr ? oldInstance->getLatency() : 0)))
        return 0;

    startEdit("Instrument: " + probe->getDescription().name);
    if (old)
        keepPluginState(*old);
    return track->setInstrument(type, {}, &undoManager).getId();
}

void Session::removeInstrument(model::ObjectId trackId)
{
    if (auto track = findTrack(trackId); track && track->getInstrument())
    {
        const auto instance = effectInstances.find(track->getInstrument()->getId());
        if (refusesLatencyChange(instance != nullptr ? instance->getLatency() : 0))
            return;
        startEdit("Remove Instrument");
        keepPluginState(*track->getInstrument());
        track->removeInstrument(&undoManager);
    }
}

void Session::removeTrack(model::ObjectId trackId)
{
    if (const int index = project.indexOfTrack(trackId); index >= 0)
    {
        startEdit("Delete Track");
        const auto track = project.getTrack(index);
        keepPluginStates(track.getEffects());
        if (const auto instrument = track.getInstrument())
            keepPluginState(*instrument);
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
    const bool onExistingTrack = files.size() == 1 && ontoTrack &&
                                 project.indexOfTrack(*ontoTrack) >= 0 &&
                                 !project.getTrack(project.indexOfTrack(*ontoTrack)).isMidi();

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

Session::MidiImport Session::importMidi(const juce::File& file, model::Ticks position)
{
    MidiImport import;
    MidiFileContent content;
    import.result = readMidiFile(file, project.getTempoMap(), content);
    if (import.result.failed())
        return import;
    if (content.parts.empty())
    {
        import.result = juce::Result::fail("\"" + file.getFileName() + "\" has no notes.");
        return import;
    }

    startEdit("Import MIDI File");

    // An empty project takes the file's tempo and metre; the positions follow them.
    if (project.getNumTracks() == 0 && (content.hasTempo || content.hasTimeSignature))
    {
        if (content.hasTempo)
            project.setTempo(content.tempo, &undoManager);
        if (content.hasTimeSignature)
            project.setTimeSignature(content.timeSignature, &undoManager);
        [[maybe_unused]] const auto again = readMidiFile(file, project.getTempoMap(), content);
    }

    const auto tempoMap = project.getTempoMap();
    position = std::max(model::Ticks{0}, position);
    for (auto& part : content.parts)
    {
        auto track = project.addTrack(part.name, model::TrackKind::midi, &undoManager);
        track.setMidiChannel(part.channel, &undoManager);
        const juce::String instrument = part.channel == 10 ? "drums" : defaultInstrument;
        if (isInstrumentType(instrument))
            track.setInstrument(instrument, {}, &undoManager);

        // To the bar line after its end.
        const auto end = position + std::max(model::Ticks{1}, part.end);
        const auto barEnd = tempoMap.barStart(tempoMap.barAt(end - 1) + 1);
        track.addMidiClip(part.name, {position, 0, barEnd - position}, std::move(part.notes),
                          std::move(part.controllers), &undoManager);
        import.trackIds.push_back(track.getId());
    }
    import.leftOut = content.leftOut;
    return import;
}

juce::Result Session::exportMidi(const juce::File& file, int type) const
{
    const auto bytes = writeMidiFile(project, type);
    if (!file.replaceWithData(bytes.getData(), bytes.getSize()))
        return juce::Result::fail("\"" + file.getFileName() + "\" could not be written.");
    return juce::Result::ok();
}

void Session::moveClip(model::ObjectId clipId, model::ObjectId toTrackId, model::Ticks position)
{
    const int trackIndex = project.indexOfTrack(toTrackId);
    const auto location = project.findClip(clipId);
    if (trackIndex < 0 || !location)
        return;

    const auto track = project.getTrack(location->trackIndex);
    if (track.getKind() != project.getTrack(trackIndex).getKind())
        return; // audio clips go on audio tracks, MIDI clips on MIDI tracks

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

std::vector<model::ObjectId> Session::duplicateClips(const std::vector<model::ObjectId>& clipIds)
{
    std::vector<model::ObjectId> copies;
    bool started = false;
    for (const auto clipId : clipIds)
    {
        const auto location = project.findClip(clipId);
        if (!location)
            continue;
        auto track = project.getTrack(location->trackIndex);
        const auto clip = track.getClip(location->clipIndex);
        const auto end = clip.getEnd();
        if (!end)
            continue; // its length is not known yet

        if (!started)
            startEdit(clipIds.size() == 1 ? "Duplicate Clip" : "Duplicate Clips");
        started = true;
        copies.push_back(track.addCopyOf(clip, *end, &undoManager).getId());
    }
    return copies;
}

model::ObjectId Session::copyClip(model::ObjectId clipId, model::ObjectId toTrackId,
                                  model::Ticks position)
{
    const auto location = project.findClip(clipId);
    const int trackIndex = project.indexOfTrack(toTrackId);
    if (!location || trackIndex < 0)
        return 0;

    auto track = project.getTrack(trackIndex);
    const auto clip = project.getTrack(location->trackIndex).getClip(location->clipIndex);
    if (track.getKind() != project.getTrack(location->trackIndex).getKind())
        return 0;

    startEdit("Copy Clip");
    return track.addCopyOf(clip, std::max(model::Ticks{0}, position), &undoManager).getId();
}

model::ObjectId Session::addMidiClip(model::ObjectId trackId, model::Ticks position,
                                     model::Ticks length)
{
    auto track = findTrack(trackId);
    if (!track || !track->isMidi())
        return 0;

    startEdit("New MIDI Clip");
    return track
        ->addMidiClip(track->getName(),
                      {std::max(model::Ticks{0}, position), 0, std::max(model::Clip::minimumLength, length)},
                      {}, {}, &undoManager)
        .getId();
}

void Session::renameClip(model::ObjectId clipId, const juce::String& name)
{
    if (const auto location = project.findClip(clipId))
    {
        auto clip = project.getTrack(location->trackIndex).getClip(location->clipIndex);
        if (clip.isMidi() && clip.getName() != name)
        {
            startEdit("Rename Clip");
            clip.setName(name, &undoManager);
        }
    }
}

std::optional<model::MidiClip> Session::findMidiClip(model::ObjectId clipId) const
{
    if (const auto location = project.findClip(clipId))
        if (const auto clip = project.getTrack(location->trackIndex).getClip(location->clipIndex);
            clip.isMidi())
            return model::MidiClip(clip.getState());
    return std::nullopt;
}

void Session::setNotes(model::ObjectId clipId, std::vector<model::Note> notes,
                       const juce::String& actionName)
{
    if (auto clip = findMidiClip(clipId))
    {
        model::normalise(notes);
        if (notes == clip->getNotes())
            return;
        startEdit(actionName);
        clip->setNotes(std::move(notes), &undoManager);
    }
}

std::int32_t Session::addNote(model::ObjectId clipId, model::Note note)
{
    auto clip = findMidiClip(clipId);
    if (!clip)
        return 0;

    auto notes = clip->getNotes();
    std::int32_t largest = 0;
    for (const auto& existing : notes)
        largest = std::max(largest, existing.id);
    note.id = largest + 1;
    notes.push_back(note);
    setNotes(clipId, std::move(notes), "Add Note");
    return note.id;
}

void Session::removeNotes(model::ObjectId clipId, const std::vector<std::int32_t>& noteIds)
{
    if (const auto clip = findMidiClip(clipId))
    {
        auto notes = clip->getNotes();
        notes.erase(std::remove_if(notes.begin(), notes.end(),
                                   [&noteIds](const model::Note& note) {
                                       return std::find(noteIds.begin(), noteIds.end(), note.id) !=
                                              noteIds.end();
                                   }),
                    notes.end());
        setNotes(clipId, std::move(notes), noteIds.size() == 1 ? "Delete Note" : "Delete Notes");
    }
}

void Session::setControllerEvents(model::ObjectId clipId, const model::ControllerType& type,
                                  std::vector<model::ControllerEvent> events,
                                  const juce::String& actionName)
{
    if (auto clip = findMidiClip(clipId))
    {
        for (auto& event : events)
            event.type = type;
        model::normalise(events);
        if (events == clip->getControllerEvents(type))
            return;
        startEdit(actionName);
        clip->setControllerEvents(type, std::move(events), &undoManager);
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
    if (auto track = findTrack(trackId))
    {
        // MIDI needs no microphone permission.
        if (armed && inputAccess == InputAccess::denied && !track->isMidi())
            return false;
        track->setArmed(armed);
        return true;
    }
    return false;
}

bool Session::needsInputs() const
{
    if (inputsRequested)
        return true;

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

    const double rate = engineRate();
    RecordingRequest request;
    request.sampleRate = rate;
    request.bitsPerSample = project.getRecordingBitsPerSample();
    request.latency = getRecordingLatency();
    request.midiLatency = deviceOutputLatency;

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        const auto input = track.getInput();
        if (track.isArmed() && track.isMidi())
            request.midiTracks.push_back({track.getId(), midiFilterOf(track)});
        else if (track.isArmed() && !input.isNone())
            request.tracks.push_back(
                {track.getId(), track.getName(), input.channel, input.getNumChannels()});
    }

    // Audio needs the microphone and a folder for its files; MIDI neither.
    if (!request.tracks.empty() || request.midiTracks.empty())
    {
        if (inputAccess == InputAccess::denied)
            return juce::Result::fail(microphoneDeniedMessage);
        if (projectFile == juce::File())
            return juce::Result::fail("Save the project first: recordings go into its folder.");
        request.folder = projectFile.getParentDirectory().getChildFile(recordingsFolderName);
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

void Session::setMidiInputDevices(juce::StringArray names, MidiDeviceLookup lookup)
{
    midiInputNames = std::move(names);
    midiDevices = std::move(lookup);
    arrangementOutOfDate = true;
    ++revision;
}

bool Session::startNotePreview(model::ObjectId trackId, int key, int velocity)
{
    const auto track = findTrack(trackId);
    return track && track->isMidi() &&
           engine.startPreview(trackId, key, engine::midi::fromSevenBit(std::clamp(velocity, 1, 127)));
}

void Session::stopNotePreview(model::ObjectId trackId, int key)
{
    if (!unsentPreviewOffs.empty() || !engine.stopPreview(trackId, key))
        unsentPreviewOffs.emplace_back(trackId, key);
}

void Session::panic()
{
    engine.panic();
}

void Session::sendPreviewOffs()
{
    std::size_t sent = 0;
    while (sent < unsentPreviewOffs.size() &&
           engine.stopPreview(unsentPreviewOffs[sent].first, unsentPreviewOffs[sent].second))
        ++sent;
    unsentPreviewOffs.erase(unsentPreviewOffs.begin(),
                            unsentPreviewOffs.begin() + static_cast<std::ptrdiff_t>(sent));
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

    // MIDI takes become MIDI clips over what the recording kept; the notes before
    // and after stay in them, hidden, as handles. A take with nothing in it adds none.
    for (const auto& take : outcome.midiTakes)
    {
        auto track = findTrack(take.trackId);
        const auto content = midiTakeContent(take, outcome.stopPosition, rate);
        const auto start = std::max({outcome.keepStart, take.firstPosition, juce::int64{0}});
        if (!track || !track->isMidi() || content.isEmpty() || outcome.keepEnd <= start)
            continue;

        if (!started)
            startEdit("Record");
        started = true;

        const auto position = model::samplesToTicks(start, rate);
        const auto offset = position - model::samplesToTicks(take.firstPosition, rate);
        const auto length = model::samplesToTicks(outcome.keepEnd, rate) - position;
        track->addMidiClip(track->getName(), {position, offset, length}, content.notes,
                           content.controllers, &undoManager);
    }

    // Takes that were discarded or kept nothing are gone from the disk.
    std::erase_if(journal, [](const JournalEntry& entry) { return !entry.file.existsAsFile(); });
    writeJournal();
}

engine::midi::InputFilter Session::midiFilterOf(const model::Track& track) const
{
    engine::midi::InputFilter filter;
    const auto input = track.getMidiInput();
    filter.channel = input.channel - 1;
    if (input.device.isNotEmpty() && midiDevices)
    {
        const int device = midiDevices(input.device);
        filter.device = device >= 0 ? device : engine::midi::InputFilter::noDevice;
    }
    return filter;
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
            if (const auto clip = track.getClip(c); !clip.isMidi())
                sources.request(clip.getAudioFile());
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
    const bool fadeIn = !freshEffects;
    const auto effectsFor = [this, fadeIn](const ChainOwner& owner,
                                           const std::vector<model::Effect>& chain) {
        return effectInstances.chainFor(owner, chain, fadeIn);
    };

    const auto samplesFor = [this](const juce::File& file) {
        instrumentSamples.request(file);
        return instrumentSamples.find(file);
    };

    auto arrangement = buildArrangement(project, sources.getSampleRate(), lookup, meterFor,
                                        effectsFor, midiDevices, samplesFor);
    effectInstances.forgetUnused();
    freshEffects = false;

    // What an edit removed fades out before it goes.
    fadeOutKeeper.keep(*arrangement, playingArrangement.get(),
                       juce::Time::getMillisecondCounterHiRes() / 1000.0);

    // While playing, what the new arrangement plays at the playhead (read ahead of
    // what is heard by the effects' latency) must be loaded before it takes over;
    // meanwhile the read-ahead serves both.
    const bool playing = engine.getTransportState() == engine::TransportState::playing;
    if (playing && playingArrangement != nullptr &&
        !engine::isArrangementReady(*arrangement,
                                    engine.getPlayheadPosition() + engine.getLatency(),
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

// Effects, buses and sends ---------------------------------------------------------------

model::EffectList Session::getEffects(const ChainOwner& owner) const
{
    if (owner.kind == ChainOwner::Kind::track)
        if (const auto track = findTrack(owner.id))
            return track->getEffects();
    if (owner.kind == ChainOwner::Kind::bus)
        if (const auto bus = findBus(owner.id))
            return bus->getEffects();
    return project.getMasterEffects();
}

std::optional<EffectLocation> Session::findEffect(model::ObjectId effectId) const
{
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        if (const int index = track.getEffects().indexOf(effectId); index >= 0)
            return EffectLocation{ChainOwner::track(track.getId()), index};
    }
    for (int b = 0; b < project.getNumBuses(); ++b)
    {
        const auto bus = project.getBus(b);
        if (const int index = bus.getEffects().indexOf(effectId); index >= 0)
            return EffectLocation{ChainOwner::bus(bus.getId()), index};
    }
    if (const int index = project.getMasterEffects().indexOf(effectId); index >= 0)
        return EffectLocation{ChainOwner::master(), index};
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        if (const auto instrument = track.getInstrument(); instrument && instrument->getId() == effectId)
            return EffectLocation{ChainOwner::track(track.getId()), -1};
    }
    return std::nullopt;
}

std::optional<model::Effect> Session::getEffect(model::ObjectId effectId) const
{
    if (const auto location = findEffect(effectId))
    {
        if (location->isInstrument())
            return findTrack(location->owner.id)->getInstrument();
        return getEffects(location->owner).getEffect(location->index);
    }
    return std::nullopt;
}

std::shared_ptr<engine::fx::EffectInstance> Session::getEffectInstance(model::ObjectId effectId) const
{
    return effectInstances.find(effectId);
}

juce::String Session::takeEditProblem()
{
    return std::exchange(editProblem, {});
}

bool Session::refusesLatencyChange(int latency)
{
    if (latency <= 0 || !isRecording())
        return false;
    editProblem = latencyWhileRecordingMessage;
    return true;
}

model::ObjectId Session::addEffect(const ChainOwner& owner, const juce::String& type, int index)
{
    const bool ownerExists = owner.kind == ChainOwner::Kind::master ||
                             (owner.kind == ChainOwner::Kind::track && findTrack(owner.id)) ||
                             (owner.kind == ChainOwner::Kind::bus && findBus(owner.id));
    const auto probe = EffectInstances::makeInstance(type, sources.getSampleRate(), effectBlockSize,
                                                     false, effectInstances.getFactory());
    if (!ownerExists || probe == nullptr || probe->getDescription().isInstrument ||
        refusesLatencyChange(probe->getLatency()))
        return 0;

    // Delay and reverb on a bus: the bus carries only the effect, fully wet.
    std::vector<std::pair<juce::String, double>> parameters;
    if (owner.kind == ChainOwner::Kind::bus && probe->getDescription().indexOf("mix") >= 0 &&
        (type == "delay" || type == "reverb"))
        parameters.emplace_back("mix", 100.0);

    startEdit("Add " + probe->getDescription().name);
    return getEffects(owner).add(type, index, parameters, &undoManager).getId();
}

bool Session::removeEffect(model::ObjectId effectId)
{
    const auto location = findEffect(effectId);
    if (!location)
        return false;
    if (location->isInstrument())
    {
        removeInstrument(location->owner.id);
        return true;
    }

    const auto instance = effectInstances.find(effectId);
    if (refusesLatencyChange(instance != nullptr ? instance->getLatency() : 0))
        return false;

    startEdit("Remove Effect");
    auto effects = getEffects(location->owner);
    keepPluginState(effects.getEffect(location->index));
    effects.remove(location->index, &undoManager);
    return true;
}

bool Session::moveEffect(model::ObjectId effectId, int toIndex)
{
    const auto location = findEffect(effectId);
    if (!location || location->isInstrument() || location->index == toIndex)
        return false;

    const auto instance = effectInstances.find(effectId);
    if (refusesLatencyChange(instance != nullptr ? instance->getLatency() : 0))
        return false;

    auto effects = getEffects(location->owner);
    effectInstances.markMoved(effectId);
    startEdit("Move Effect");
    effects.move(location->index, std::clamp(toIndex, 0, effects.getNumEffects() - 1), &undoManager);
    return true;
}

void Session::setEffectBypassed(model::ObjectId effectId, bool bypassed)
{
    if (auto effect = getEffect(effectId))
    {
        startEdit(bypassed ? "Bypass Effect" : "Enable Effect");
        effect->setBypassed(bypassed, &undoManager);
    }
}

void Session::setEffectParameter(model::ObjectId effectId, const juce::String& parameterId,
                                 double value)
{
    if (auto effect = getEffect(effectId))
    {
        startEdit("Change " + parameterId);
        // A plugin hears of it from the model (pushToPlugin()), as it does of undo.
        effect->setParameter(parameterId, value, &undoManager);
    }
}

void Session::setInstrumentFile(model::ObjectId effectId, const juce::String& key,
                                const juce::File& file)
{
    if (auto effect = getEffect(effectId); effect && effect->getFile(key) != file)
    {
        startEdit(file == juce::File() ? "Remove Sample" : "Load Sample");
        effect->setFile(key, file, &undoManager);
        instrumentSamples.request(file);
    }
}

void Session::reviveEffect(model::ObjectId effectId)
{
    effectInstances.revive(effectId);
    arrangementOutOfDate = true;
}

std::optional<model::Bus> Session::findBus(model::ObjectId busId) const
{
    const int index = project.indexOfBus(busId);
    return index >= 0 ? std::optional<model::Bus>(project.getBus(index)) : std::nullopt;
}

model::ObjectId Session::addBus(const juce::String& name)
{
    startEdit("New Bus");
    const auto busName = name.isNotEmpty() ? name : "Bus " + juce::String(project.getNumBuses() + 1);
    return project.addBus(busName, &undoManager).getId();
}

void Session::removeBus(model::ObjectId busId)
{
    if (const int index = project.indexOfBus(busId); index >= 0)
    {
        startEdit("Delete Bus");
        keepPluginStates(project.getBus(index).getEffects());
        project.removeBus(index, &undoManager);
    }
}

void Session::renameBus(model::ObjectId busId, const juce::String& name)
{
    if (auto bus = findBus(busId))
    {
        startEdit("Rename Bus");
        bus->setName(name, &undoManager);
    }
}

void Session::setBusVolume(model::ObjectId busId, double gain)
{
    if (auto bus = findBus(busId))
    {
        startEdit("Bus Volume");
        bus->setVolume(gain, &undoManager);
    }
}

void Session::setBusPan(model::ObjectId busId, double pan)
{
    if (auto bus = findBus(busId))
    {
        startEdit("Bus Pan");
        bus->setPan(pan, &undoManager);
    }
}

void Session::setBusMuted(model::ObjectId busId, bool muted)
{
    if (auto bus = findBus(busId))
    {
        startEdit(muted ? "Mute Bus" : "Unmute Bus");
        bus->setMuted(muted, &undoManager);
    }
}

std::shared_ptr<engine::dsp::PeakLevelMeter> Session::getBusMeter(model::ObjectId busId)
{
    return getTrackMeter(busId); // ids are unique across tracks and buses
}

model::ObjectId Session::addSend(model::ObjectId trackId, model::ObjectId busId)
{
    auto track = findTrack(trackId);
    if (!track || !findBus(busId))
        return 0;

    if (const int existing = track->indexOfSendTo(busId); existing >= 0)
        return track->getSend(existing).getId();

    startEdit("Add Send");
    return track->addSend(busId, &undoManager).getId();
}

void Session::removeSend(model::ObjectId trackId, model::ObjectId sendId)
{
    if (auto track = findTrack(trackId))
        if (const int index = track->indexOfSend(sendId); index >= 0)
        {
            startEdit("Remove Send");
            track->removeSend(index, &undoManager);
        }
}

void Session::setSendLevel(model::ObjectId trackId, model::ObjectId sendId, double gain)
{
    if (auto track = findTrack(trackId))
        if (const int index = track->indexOfSend(sendId); index >= 0)
        {
            startEdit("Send Level");
            track->getSend(index).setLevel(gain, &undoManager);
        }
}

void Session::setSendPreFader(model::ObjectId trackId, model::ObjectId sendId, bool preFader)
{
    if (auto track = findTrack(trackId))
        if (const int index = track->indexOfSend(sendId); index >= 0)
        {
            startEdit(preFader ? "Pre-Fader Send" : "Post-Fader Send");
            track->getSend(index).setPreFader(preFader, &undoManager);
        }
}

// Automation ------------------------------------------------------------------------------

model::Automation Session::getAutomation(const ChainOwner& owner) const
{
    if (owner.kind == ChainOwner::Kind::track)
        if (const auto track = findTrack(owner.id))
            return track->getAutomation();
    if (owner.kind == ChainOwner::Kind::bus)
        if (const auto bus = findBus(owner.id))
            return bus->getAutomation();
    return project.getMasterAutomation();
}

void Session::setAutomationMode(const ChainOwner& owner, model::AutomationMode mode)
{
    getAutomation(owner).setMode(mode);
}

int Session::addAutomationPoint(const ChainOwner& owner, const model::AutomationTarget& target,
                                model::AutomationPointData point)
{
    startEdit("Add Automation Point");
    return getAutomation(owner).getOrAddLane(target, &undoManager).addPoint(point, &undoManager);
}

int Session::setAutomationPoint(const ChainOwner& owner, const model::AutomationTarget& target,
                                int index, model::AutomationPointData point)
{
    auto automation = getAutomation(owner);
    const int lane = automation.indexOfLane(target);
    if (lane < 0)
        return -1;

    startEdit("Move Automation Point");
    return automation.getLane(lane).setPoint(index, point, &undoManager);
}

void Session::removeAutomationPoint(const ChainOwner& owner, const model::AutomationTarget& target,
                                    int index)
{
    auto automation = getAutomation(owner);
    if (const int lane = automation.indexOfLane(target); lane >= 0)
    {
        startEdit("Delete Automation Point");
        automation.getLane(lane).removePoint(index, &undoManager);
    }
}

void Session::clearAutomation(const ChainOwner& owner, const model::AutomationTarget& target)
{
    auto automation = getAutomation(owner);
    if (const int lane = automation.indexOfLane(target); lane >= 0)
    {
        startEdit("Clear Automation");
        automation.removeLane(lane, &undoManager);
    }
}

std::optional<double> Session::getAutomatedValue(const ChainOwner& owner,
                                                 const model::AutomationTarget& target) const
{
    const auto automation = getAutomation(owner);
    const int lane = automation.indexOfLane(target);
    if (automation.getMode() != model::AutomationMode::read || lane < 0)
        return std::nullopt;
    return automationValueAt(automation.getLane(lane), getPlayhead());
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

void Session::valueTreePropertyChanged(juce::ValueTree& tree, const juce::Identifier& property)
{
    pushToPlugin(tree, property);
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

// Plugins ------------------------------------------------------------------------------

model::ObjectId Session::addPlugin(const ChainOwner& owner, const juce::PluginDescription& plugin, int index)
{
    const bool ownerExists = owner.kind == ChainOwner::Kind::master ||
                             (owner.kind == ChainOwner::Kind::track && findTrack(owner.id)) ||
                             (owner.kind == ChainOwner::Kind::bus && findBus(owner.id));
    if (!ownerExists || pluginInstances == nullptr || plugin.isInstrument)
        return 0;
    // Its latency is known once it has loaded: none may be added while recording.
    if (isRecording())
    {
        editProblem = latencyWhileRecordingMessage;
        return 0;
    }

    startEdit("Add " + plugin.name);
    auto effect = getEffects(owner).add(model::pluginEffectType, index, {}, &undoManager);
    effect.setPlugin(PluginInstances::referenceTo(plugin), &undoManager);
    return effect.getId();
}

model::ObjectId Session::setInstrumentPlugin(model::ObjectId trackId, const juce::PluginDescription& plugin)
{
    auto track = findTrack(trackId);
    if (!track || !track->isMidi() || pluginInstances == nullptr || !plugin.isInstrument)
        return 0;
    if (isRecording())
    {
        editProblem = latencyWhileRecordingMessage;
        return 0;
    }

    startEdit("Instrument: " + plugin.name);
    if (const auto old = track->getInstrument())
        keepPluginState(*old);
    auto instrument = track->setInstrument(model::pluginEffectType, {}, &undoManager);
    instrument.setPlugin(PluginInstances::referenceTo(plugin), &undoManager);
    return instrument.getId();
}

PluginStatus Session::getPluginStatus(model::ObjectId effectId) const
{
    return pluginInstances != nullptr ? pluginInstances->getStatus(effectId) : PluginStatus::missing;
}

juce::StringArray Session::getUnavailablePlugins() const
{
    return pluginInstances != nullptr ? pluginInstances->getUnavailable() : juce::StringArray();
}

juce::String Session::getPluginProblem(model::ObjectId effectId) const
{
    return pluginInstances != nullptr ? pluginInstances->getProblem(effectId)
                                      : juce::String("plugins are not available here");
}

engine::plugins::PluginEffect* Session::findPlugin(model::ObjectId effectId) const
{
    return pluginInstances != nullptr ? pluginInstances->findPlugin(effectId) : nullptr;
}

std::shared_ptr<engine::plugins::PluginEffect> Session::sharePlugin(model::ObjectId effectId) const
{
    return pluginInstances != nullptr ? pluginInstances->sharePlugin(effectId) : nullptr;
}

void Session::pluginsChanged()
{
    if (pluginInstances == nullptr)
        return;
    options.plugins->readFolders(*options.catalogue);
    pluginInstances->retryMissing();
    arrangementOutOfDate = true;
    ++revision;
}

void Session::holdBackPlugins(const juce::StringArray& displayNames)
{
    if (pluginInstances != nullptr)
        for (const auto& name : displayNames)
            pluginInstances->holdBack(name);
}

void Session::allowPlugin(model::ObjectId effectId)
{
    if (pluginInstances != nullptr)
        pluginInstances->allow(effectId);
    arrangementOutOfDate = true;
}

void Session::allowAllPlugins()
{
    if (pluginInstances != nullptr)
        pluginInstances->allowAll();
    arrangementOutOfDate = true;
}

void Session::takePluginLoads()
{
    const double now = juce::Time::getMillisecondCounterHiRes() / 1000.0;
    if (now - lastPluginLoads < 0.25)
        return;
    lastPluginLoads = now;
    std::vector<PluginLoads::Reading> readings;
    for (const auto& [effectId, plugin] : pluginInstances->getPlugins())
    {
        const auto load = plugin->takeLoad();
        readings.push_back({effectId, plugin->getPluginDescription().name, load.average, load.peak, load.overruns});
    }
    pluginLoads.add(readings, now);
}

const PluginCatalogue* Session::getPluginCatalogue() const noexcept
{
    return pluginInstances != nullptr ? options.catalogue : nullptr;
}

void Session::takePluginChanges()
{
    const auto now = juce::Time::getMillisecondCounter();
    for (const auto& [effectId, plugin] : pluginInstances->getPlugins())
    {
        if (plugin->takeStateChanged())
        {
            unsavedChanges = true;
            ++revision;
        }

        plugin->takeParameterReports(pluginReports);
        if (pluginReports.empty())
            continue;
        auto effect = getEffect(effectId);
        if (!effect)
            continue;

        const auto& parameters = plugin->getDescription().parameters;
        for (const auto& report : pluginReports)
        {
            const auto& info = parameters[static_cast<std::size_t>(report.index)];
            if (!info.automatable)
                continue; // a meter, or something read-only: not an edit

            // One undo step per gesture in the plugin's editor; changes without a
            // gesture are one step while they keep coming (half a second apart at
            // most).
            const bool sameEdit = pluginEdit.effectId == effectId && pluginEdit.index == report.index &&
                                  (pluginEdit.gesture || now - pluginEdit.lastChange < 500);
            if (!sameEdit)
                undoManager.beginNewTransaction("Change " + info.name);

            writingFromPlugin = true;
            effect->setParameter(info.id, report.value, &undoManager);
            writingFromPlugin = false;

            pluginEdit = {effectId, report.index, now, report.inGesture};
            if (report.gestureEnded && !report.inGesture)
                pluginEdit = {}; // the next change starts a step of its own
        }
    }
}

void Session::pushToPlugin(const juce::ValueTree& tree, const juce::Identifier& property)
{
    // A parameter the model changed (the generic editor, undo, redo) reaches the
    // plugin; one the plugin changed itself is already there.
    if (writingFromPlugin || pluginInstances == nullptr)
        return;
    const auto effect = model::Effect::ofParameters(tree);
    if (!effect || effect->getType() != model::pluginEffectType)
        return;
    auto* plugin = pluginInstances->findPlugin(effect->getId());
    if (plugin == nullptr)
        return;
    const auto& description = plugin->getDescription();
    const int index = description.indexOf(property.toString());
    if (index < 0)
        return;
    const auto value = effect->getParameter(property.toString());
    plugin->setParameter(index, value ? static_cast<float>(*value)
                                      : description.parameters[static_cast<std::size_t>(index)].defaultValue);
}

void Session::keepPluginState(const model::Effect& effect)
{
    // Not undoable itself: undoing the removal brings the effect back with the state
    // it had when it went (undoing this too would bring back an older one).
    if (auto* plugin = findPlugin(effect.getId()))
        model::Effect(effect).setExtraState(plugin->getExtraState());
}

void Session::keepPluginStates(const model::EffectList& effects)
{
    for (int i = 0; i < effects.getNumEffects(); ++i)
        keepPluginState(effects.getEffect(i));
}

void Session::storePluginStates()
{
    // Not an edit: the file gets what each plugin holds now.
    if (pluginInstances == nullptr)
        return;
    writingFromPlugin = true;
    for (const auto& [effectId, plugin] : pluginInstances->getPlugins())
        if (auto effect = getEffect(effectId))
            effect->setExtraState(plugin->getExtraState());
    writingFromPlugin = false;
}

Session::ExportPlugins Session::makeExportPlugins(double sampleRate) const
{
    ExportPlugins result;
    const auto add = [&](const model::Effect& effect) {
        const auto reference = effect.getPlugin();
        if (effect.getType() != model::pluginEffectType || !reference)
            return;
        const auto info = pluginInstances != nullptr ? pluginInstances->findInCatalogue(*reference)
                                                     : std::optional<PluginInfo>{};
        juce::String error;
        auto instance = info && info->isSupported()
                            ? options.plugins->loadNow(info->description, sampleRate, effectBlockSize, error)
                            : nullptr;
        auto created = instance != nullptr ? engine::plugins::PluginEffect::create(std::move(instance))
                                           : std::variant<std::unique_ptr<engine::plugins::PluginEffect>,
                                                          juce::String>(juce::String("missing"));
        if (!std::holds_alternative<std::unique_ptr<engine::plugins::PluginEffect>>(created))
        {
            result.missing.add(reference->name);
            return;
        }

        std::shared_ptr<engine::plugins::PluginEffect> plugin =
            std::move(std::get<std::unique_ptr<engine::plugins::PluginEffect>>(created));
        engine::fx::ProcessSetup setup;
        setup.sampleRate = sampleRate;
        setup.maximumBlockSize = effectBlockSize;
        setup.offline = true;
        auto hosted = std::make_shared<engine::fx::EffectInstance>(
            std::make_unique<engine::plugins::PluginSlot>(plugin), setup, false);

        // What it holds now if it plays, else what the project holds.
        const auto* playing = pluginInstances->findPlugin(effect.getId());
        const auto state = playing != nullptr ? playing->getExtraState() : effect.getExtraState();
        plugin->setExtraState(state);
        for (const auto& [parameterId, value] : effect.getParameters())
            if (const int index = plugin->getDescription().indexOf(parameterId); index >= 0 && playing == nullptr)
                plugin->setParameter(index, static_cast<float>(value));
        result.instances[effect.getId()] = std::move(hosted);
    };

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        if (const auto instrument = track.getInstrument())
            add(*instrument);
        for (int i = 0; i < track.getEffects().getNumEffects(); ++i)
            add(track.getEffects().getEffect(i));
    }
    for (int b = 0; b < project.getNumBuses(); ++b)
        for (int i = 0; i < project.getBus(b).getEffects().getNumEffects(); ++i)
            add(project.getBus(b).getEffects().getEffect(i));
    for (int i = 0; i < project.getMasterEffects().getNumEffects(); ++i)
        add(project.getMasterEffects().getEffect(i));
    return result;
}

} // namespace daw::controller
