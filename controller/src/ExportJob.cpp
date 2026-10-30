#include "daw/controller/ExportJob.h"

#include "daw/controller/ArrangementBuilder.h"
#include "daw/controller/Session.h"
#include "daw/engine/io/SourcePool.h"

namespace daw::controller
{

ExportJob::ExportJob(const Session& session, ExportSettings settingsToUse)
    : projectState(session.getProject().getState().createCopy())
    , settings(std::move(settingsToUse))
{
    if (!juce::exactlyEqual(settings.sampleRate, session.getSampleRate()))
        return;

    const auto& project = session.getProject();
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        for (int c = 0; c < track.getNumClips(); ++c)
        {
            const auto file = track.getClip(c).getAudioFile();
            if (const auto* source = session.findSource(file); source != nullptr && source->audio)
                loadedSources[file.getFullPathName()] = source->audio;
        }
    }
}

engine::io::BounceResult ExportJob::run(const std::function<bool(double)>& keepGoing)
{
    const model::Project project(projectState);

    // Load, at the export's rate, the files that were not loaded at it already.
    engine::io::SourcePool pool(settings.sampleRate);
    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        for (int c = 0; c < track.getNumClips(); ++c)
        {
            const auto file = track.getClip(c).getAudioFile();
            if (!loadedSources.contains(file.getFullPathName()))
                pool.request(file);
        }
    }

    while (!pool.waitForLoads(50))
    {
        if (keepGoing && !keepGoing(0.0))
        {
            engine::io::BounceResult cancelled;
            cancelled.cancelled = true;
            return cancelled;
        }
    }
    pool.collectFinishedLoads();

    const auto lookup = [this, &pool](const juce::File& file) {
        if (const auto found = loadedSources.find(file.getFullPathName());
            found != loadedSources.end())
            return found->second;

        const auto* source = pool.find(file);
        return source != nullptr ? source->audio : nullptr;
    };

    const auto arrangement = buildArrangement(project, settings.sampleRate, lookup);

    engine::io::BounceSettings bounce;
    bounce.file = settings.file;
    bounce.bitsPerSample = settings.bitsPerSample;
    bounce.dither = settings.dither;
    bounce.start = 0;
    bounce.end = arrangement->length;

    if (settings.range == ExportSettings::Range::loopRegion &&
        arrangement->loopEnd > arrangement->loopStart)
    {
        bounce.start = arrangement->loopStart;
        bounce.end = arrangement->loopEnd;
    }

    return engine::io::bounceToWav(*arrangement, bounce, keepGoing);
}

} // namespace daw::controller
