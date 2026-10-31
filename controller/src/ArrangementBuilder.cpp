#include "daw/controller/ArrangementBuilder.h"

#include "daw/engine/dsp/PanLaw.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace daw::controller
{

namespace
{

engine::InputMonitoring toEngine(model::MonitoringMode mode)
{
    switch (mode)
    {
    case model::MonitoringMode::off:
        return engine::InputMonitoring::off;
    case model::MonitoringMode::automatic:
        return engine::InputMonitoring::automatic;
    case model::MonitoringMode::on:
        return engine::InputMonitoring::on;
    }

    return engine::InputMonitoring::off;
}

} // namespace

std::unique_ptr<engine::Arrangement> buildArrangement(const model::Project& project,
                                                      double sampleRate,
                                                      const SourceLookup& sources,
                                                      const MeterLookup& meters)
{
    jassert(sampleRate > 0.0);

    auto arrangement = std::make_unique<engine::Arrangement>();
    arrangement->sampleRate = sampleRate;
    arrangement->masterGain = static_cast<float>(project.getMasterVolume());

    const auto toSamples = [sampleRate](model::Ticks ticks) {
        return model::ticksToSamples(ticks, sampleRate);
    };
    const int fadeLength =
        std::max(1, static_cast<int>(std::lround(engine::clipEdgeFadeSeconds * sampleRate)));

    std::set<std::shared_ptr<const engine::AudioSource>> used;

    for (int trackIndex = 0; trackIndex < project.getNumTracks(); ++trackIndex)
    {
        const auto track = project.getTrack(trackIndex);

        engine::ArrangementTrack arrangementTrack;
        arrangementTrack.id = track.getId();
        if (meters)
            arrangementTrack.meter = meters(track.getId());

        const auto input = track.getInput();
        arrangementTrack.inputChannel = input.channel;
        arrangementTrack.numInputChannels = input.getNumChannels();
        arrangementTrack.monitoring = toEngine(track.getMonitoring());
        arrangementTrack.armed = track.isArmed();

        if (project.isTrackAudible(trackIndex))
        {
            const auto pan = engine::dsp::panGains(track.getPan());
            arrangementTrack.leftGain = static_cast<float>(track.getVolume() * pan.left);
            arrangementTrack.rightGain = static_cast<float>(track.getVolume() * pan.right);
        }
        else
        {
            arrangementTrack.leftGain = 0.0f;
            arrangementTrack.rightGain = 0.0f;
        }

        std::vector<engine::ClipPlacement> placements;
        for (int clipIndex = 0; clipIndex < track.getNumClips(); ++clipIndex)
        {
            const auto clip = track.getClip(clipIndex);
            auto source = sources ? sources(clip.getAudioFile()) : nullptr;

            const juce::int64 start = toSamples(clip.getPosition());
            const juce::int64 sourceStart = toSamples(clip.getOffset());
            juce::int64 length = 0;

            if (const auto end = clip.getEnd())
                length = toSamples(*end) - start;
            else if (source != nullptr)
                length = source->getLength() - sourceStart;

            if (source != nullptr)
            {
                const juce::int64 available = source->getLength() - sourceStart;
                if (length >= available - 1)
                    length = available;
            }

            if (length <= 0)
                continue;

            if (source != nullptr)
                used.insert(source);

            placements.push_back({std::move(source), start, start + length, sourceStart});
            arrangement->length = std::max(arrangement->length, start + length);
        }

        arrangementTrack.segments = engine::flattenClips(placements, fadeLength);
        arrangement->tracks.push_back(std::move(arrangementTrack));
    }

    arrangement->sources.assign(used.begin(), used.end());

    if (project.getLoopEnd() > project.getLoopStart())
    {
        arrangement->loopStart = toSamples(project.getLoopStart());
        arrangement->loopEnd = toSamples(project.getLoopEnd());
    }

    arrangement->updateLayoutHash();
    return arrangement;
}

} // namespace daw::controller
