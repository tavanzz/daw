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
}

engine::io::BounceResult ExportJob::run(const std::function<bool(double)>& keepGoing)
{
    const model::Project project(projectState);

    // Its own streams, at the export's rate: the session's belong to the audio thread,
    // which never waits for the disk; this thread may, so it reads what it needs as
    // it goes (synchronous streaming) and no file is held whole in memory.
    engine::io::StreamingOptions streaming;
    streaming.mode = engine::io::DiskStreamer::Mode::synchronous;
    streaming.computePeaks = false;
    engine::io::SourcePool pool(settings.sampleRate, 2, streaming);

    for (int t = 0; t < project.getNumTracks(); ++t)
    {
        const auto track = project.getTrack(t);
        for (int c = 0; c < track.getNumClips(); ++c)
            pool.request(track.getClip(c).getAudioFile());
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

    const auto lookup = [&pool](const juce::File& file) {
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
