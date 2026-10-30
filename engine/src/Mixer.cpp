#include "daw/engine/Mixer.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{

namespace
{

// Multiplies the samples of [from, from + length) that fall in the segment's fades
// by their fade gains. Each gain depends only on the sample's place in the segment.
void applyFades(const ClipSegment& segment, juce::int64 from, int length, float* left,
                float* right) noexcept
{
    const juce::int64 to = from + length;

    if (segment.fadeInLength > 0)
    {
        const juce::int64 fadeEnd = segment.start + segment.fadeInLength;
        const auto fullLevel = static_cast<float>(segment.fadeInLength);

        for (juce::int64 position = from; position < std::min(to, fadeEnd); ++position)
        {
            const auto level = static_cast<float>(position - segment.start + 1);
            const float gain = level / fullLevel;
            const auto i = static_cast<std::size_t>(position - from);
            left[i] *= gain;
            right[i] *= gain;
        }
    }

    if (segment.fadeOutLength > 0)
    {
        const juce::int64 fadeStart = segment.end - segment.fadeOutLength;
        const auto fullLevel = static_cast<float>(segment.fadeOutLength);

        for (juce::int64 position = std::max(from, fadeStart); position < to; ++position)
        {
            const auto level = static_cast<float>(segment.end - position);
            const float gain = level / fullLevel;
            const auto i = static_cast<std::size_t>(position - from);
            left[i] *= gain;
            right[i] *= gain;
        }
    }
}

} // namespace

MixState MixState::startingAt(const Arrangement& arrangement)
{
    MixState state;
    state.tracks.resize(arrangement.tracks.size());

    for (std::size_t i = 0; i < arrangement.tracks.size(); ++i)
    {
        state.tracks[i].left.reset(arrangement.tracks[i].leftGain);
        state.tracks[i].right.reset(arrangement.tracks[i].rightGain);
    }

    state.master.reset(arrangement.masterGain);
    return state;
}

void MixState::continueFrom(const MixState& previous, const Arrangement& previousArrangement,
                            const Arrangement& arrangement, int rampLength) noexcept
{
    jassert(tracks.size() == arrangement.tracks.size());
    jassert(previous.tracks.size() == previousArrangement.tracks.size());

    const auto& before = previousArrangement.tracks;

    for (std::size_t i = 0; i < arrangement.tracks.size(); ++i)
    {
        const auto& track = arrangement.tracks[i];

        // Tracks usually keep their order: look at the same index first.
        std::size_t match = i;
        if (match >= before.size() || before[match].id != track.id)
        {
            match = before.size();
            for (std::size_t j = 0; j < before.size(); ++j)
                if (before[j].id == track.id)
                    match = j;
        }

        if (match < before.size())
        {
            tracks[i] = previous.tracks[match];
            tracks[i].left.setTarget(track.leftGain, rampLength);
            tracks[i].right.setTarget(track.rightGain, rampLength);
        }
    }

    master = previous.master;
    master.setTarget(arrangement.masterGain, rampLength);
}

void Mixer::prepare(double sampleRate, int maximumBlockSize)
{
    jassert(sampleRate > 0.0 && maximumBlockSize > 0);
    gainRampLength = std::max(1, static_cast<int>(std::lround(gainRampSeconds * sampleRate)));
    scratch.setSize(2, maximumBlockSize);
}

void Mixer::render(const Arrangement& arrangement, MixState& state, juce::int64 timelineStart,
                   juce::AudioBuffer<float>& output, int outputStart, int numSamples) noexcept
{
    jassert(numSamples <= scratch.getNumSamples());
    jassert(state.tracks.size() == arrangement.tracks.size());
    jassert(outputStart >= 0 && outputStart + numSamples <= output.getNumSamples());

    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        output.clear(channel, outputStart, numSamples);

    if (output.getNumChannels() < 2 || numSamples <= 0)
        return;

    float* const mix[] = {output.getWritePointer(0, outputStart),
                          output.getWritePointer(1, outputStart)};

    for (std::size_t i = 0; i < arrangement.tracks.size(); ++i)
    {
        const auto& track = arrangement.tracks[i];
        renderTrack(track, state.tracks[i], timelineStart, numSamples);

        // A plain add, never a fused multiply-add, so that rounding is the same
        // whether a sample ends up in a vectorised part or not.
        juce::FloatVectorOperations::add(mix[0], scratch.getReadPointer(0), numSamples);
        juce::FloatVectorOperations::add(mix[1], scratch.getReadPointer(1), numSamples);
    }

    state.master.apply(mix, 2, numSamples);
}

void Mixer::renderTrack(const ArrangementTrack& track, MixState::TrackGains& gains,
                        juce::int64 timelineStart, int numSamples) noexcept
{
    float* const channels[] = {scratch.getWritePointer(0), scratch.getWritePointer(1)};
    const juce::int64 timelineEnd = timelineStart + numSamples;

    juce::FloatVectorOperations::clear(channels[0], numSamples);
    juce::FloatVectorOperations::clear(channels[1], numSamples);

    // A silent track reads nothing; its gains still move on, as if it had played.
    const bool silent = !gains.left.isRamping() && !gains.right.isRamping() &&
                        juce::exactlyEqual(gains.left.getTarget(), 0.0f) &&
                        juce::exactlyEqual(gains.right.getTarget(), 0.0f);

    if (silent)
    {
        gains.left.skip(numSamples);
        gains.right.skip(numSamples);
        return;
    }

    // The first segment that ends after timelineStart: segments are sorted and
    // disjoint, so their ends are sorted too.
    const auto first = std::partition_point(
        track.segments.begin(), track.segments.end(),
        [timelineStart](const ClipSegment& segment) { return segment.end <= timelineStart; });

    for (auto segment = first; segment != track.segments.end() && segment->start < timelineEnd;
         ++segment)
    {
        const juce::int64 from = std::max(segment->start, timelineStart);
        const juce::int64 to = std::min(segment->end, timelineEnd);
        const auto offset = static_cast<int>(from - timelineStart);
        const auto length = static_cast<int>(to - from);
        const juce::int64 sourcePosition = segment->sourceStart + (from - segment->start);

        // Mono sources play on both channels; sources beyond stereo give their first two.
        const auto* source = segment->source;
        source->read(0, sourcePosition, channels[0] + offset, length);
        if (source->getNumChannels() > 1)
            source->read(1, sourcePosition, channels[1] + offset, length);
        else
            juce::FloatVectorOperations::copy(channels[1] + offset, channels[0] + offset, length);

        applyFades(*segment, from, length, channels[0] + offset, channels[1] + offset);
    }

    gains.left.apply(channels, 1, numSamples);
    gains.right.apply(channels + 1, 1, numSamples);

    if (track.meter != nullptr)
        track.meter->process(scratch, 0, numSamples);
}

} // namespace daw::engine
