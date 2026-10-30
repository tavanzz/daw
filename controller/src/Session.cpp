#include "daw/controller/Session.h"

#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/TimelineViewport.h"
#include "daw/model/ProjectFile.h"

#include <algorithm>
#include <set>

namespace daw::controller
{

Session::Session(engine::Engine& engineToDrive)
    : engine(engineToDrive)
{
    if (engine.getSampleRate() > 0.0)
        sources.setSampleRate(engine.getSampleRate());

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
    sources.collectFinishedLoads();

    if (arrangementOutOfDate)
        rebuildArrangement();

    engine.collectGarbage();
}

bool Session::waitForLoads(int timeoutMilliseconds)
{
    const bool idle = sources.waitForLoads(timeoutMilliseconds);
    update();
    return idle;
}

void Session::setSampleRate(double sampleRate)
{
    if (!(sampleRate > 0.0) || juce::exactlyEqual(sampleRate, sources.getSampleRate()))
        return;

    sources.setSampleRate(sampleRate);
    arrangementOutOfDate = true;
}

// Project files ----------------------------------------------------------------------

void Session::newProject()
{
    replaceProject(model::Project());
    projectFile = juce::File();
}

juce::Result Session::openProject(const juce::File& file)
{
    auto loaded = model::loadProject(file);
    if (!loaded.succeeded())
        return juce::Result::fail(loaded.error);

    replaceProject(std::move(*loaded.project));
    projectFile = file;
    return juce::Result::ok();
}

juce::Result Session::saveProject(const juce::File& file)
{
    const auto target = file.withFileExtension(model::projectFileExtension);
    const auto result = model::saveProject(project, target);

    if (result.wasOk())
    {
        projectFile = target;
        unsavedChanges = false;
    }

    return result;
}

void Session::replaceProject(model::Project newProject)
{
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
    engine.pause();
}

void Session::stop()
{
    engine.stop();
}

void Session::seek(model::Ticks position)
{
    engine.seek(model::ticksToSamples(std::max(model::Ticks{0}, position), engineRate()));
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

    engine.setArrangement(buildArrangement(project, sources.getSampleRate(), lookup, meterFor));
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
