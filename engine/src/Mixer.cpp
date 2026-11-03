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

// The track with that id, looking at the same position first: tracks usually keep
// their order from one arrangement to the next.
const ArrangementTrack* findTrack(const Arrangement& arrangement, juce::int64 id,
                                  std::size_t hint) noexcept
{
    const auto& tracks = arrangement.tracks;
    if (hint < tracks.size() && tracks[hint].id == id)
        return &tracks[hint];

    for (const auto& track : tracks)
        if (track.id == id)
            return &track;

    return nullptr;
}

// The bus with that id, looking at hint first; -1 if there is none.
int findBus(const Arrangement& arrangement, juce::int64 id, int hint) noexcept
{
    const auto& buses = arrangement.buses;
    if (hint >= 0 && static_cast<std::size_t>(hint) < buses.size() &&
        buses[static_cast<std::size_t>(hint)].id == id)
        return hint;

    for (std::size_t i = 0; i < buses.size(); ++i)
        if (buses[i].id == id)
            return static_cast<int>(i);

    return -1;
}

} // namespace

MixState MixState::startingAt(const Arrangement& arrangement, int busBlockSize)
{
    MixState state;
    state.tracks.resize(arrangement.tracks.size());

    for (std::size_t i = 0; i < arrangement.tracks.size(); ++i)
    {
        const auto& track = arrangement.tracks[i];
        auto& gains = state.tracks[i];
        gains.left.reset(track.leftGain);
        gains.right.reset(track.rightGain);

        gains.sends.resize(track.sends.size());
        for (std::size_t k = 0; k < track.sends.size(); ++k)
            gains.sends[k].reset(track.sends[k].gain);
    }

    state.buses.resize(arrangement.buses.size());
    for (std::size_t i = 0; i < arrangement.buses.size(); ++i)
    {
        state.buses[i].left.reset(arrangement.buses[i].leftGain);
        state.buses[i].right.reset(arrangement.buses[i].rightGain);
    }

    if (!arrangement.buses.empty())
        state.busInputs.setSize(2 * static_cast<int>(arrangement.buses.size()),
                                std::max(1, busBlockSize));

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
        const auto* previousTrack = findTrack(previousArrangement, track.id, i);
        if (previousTrack == nullptr)
            continue;

        const auto match = static_cast<std::size_t>(previousTrack - before.data());
        const auto& old = previous.tracks[match];
        auto& gains = tracks[i];

        // Member by member: assigning the whole struct would copy (allocate) the
        // vector of sends.
        gains.left = old.left;
        gains.right = old.right;
        gains.timeline = old.timeline;
        gains.input = old.input;
        gains.left.setTarget(track.leftGain, rampLength);
        gains.right.setTarget(track.rightGain, rampLength);

        for (std::size_t k = 0; k < track.sends.size(); ++k)
        {
            for (std::size_t j = 0; j < previousTrack->sends.size(); ++j)
            {
                if (previousTrack->sends[j].id == track.sends[k].id)
                {
                    gains.sends[k] = old.sends[j];
                    gains.sends[k].setTarget(track.sends[k].gain, rampLength);
                }
            }
        }
    }

    for (std::size_t i = 0; i < arrangement.buses.size(); ++i)
    {
        const auto& bus = arrangement.buses[i];
        for (std::size_t j = 0; j < previousArrangement.buses.size(); ++j)
        {
            if (previousArrangement.buses[j].id == bus.id)
            {
                buses[i] = previous.buses[j];
                buses[i].left.setTarget(bus.leftGain, rampLength);
                buses[i].right.setTarget(bus.rightGain, rampLength);
            }
        }
    }

    master = previous.master;
    master.setTarget(arrangement.masterGain, rampLength);
}

bool MixState::isHearingInput() const noexcept
{
    return std::any_of(tracks.begin(), tracks.end(), [](const TrackGains& gains) {
        return gains.input.isRamping() || gains.input.getTarget() > 0.0f;
    });
}

void Mixer::prepare(double sampleRate, int maximumBlockSize)
{
    jassert(sampleRate > 0.0 && maximumBlockSize > 0);
    gainRampLength = std::max(1, static_cast<int>(std::lround(gainRampSeconds * sampleRate)));
    monitorRampLength =
        std::max(1, static_cast<int>(std::lround(monitorRampSeconds * sampleRate)));
    scratch.setSize(2, maximumBlockSize);
    inputScratch.setSize(2, maximumBlockSize);
    oldClips.setSize(2, maximumBlockSize);
    sendScratch.setSize(2, maximumBlockSize);
    gainScratch.assign(static_cast<std::size_t>(maximumBlockSize), 0.0f);
    effectScratch.prepare(maximumBlockSize);
}

void Mixer::render(const Arrangement& arrangement, MixState& state, juce::int64 timelineStart,
                   juce::AudioBuffer<float>& output, int outputStart, int numSamples,
                   const MixContext& context) noexcept
{
    jassert(numSamples <= scratch.getNumSamples());
    jassert(state.tracks.size() == arrangement.tracks.size());
    jassert(state.buses.size() == arrangement.buses.size());
    jassert(outputStart >= 0 && outputStart + numSamples <= output.getNumSamples());

    for (int channel = 0; channel < output.getNumChannels(); ++channel)
        output.clear(channel, outputStart, numSamples);

    if (output.getNumChannels() < 2 || numSamples <= 0)
        return;

    // The buses sum a limited stretch at once.
    const int pieceLimit = arrangement.buses.empty() ? numSamples
                                                     : std::max(1, state.busInputs.getNumSamples());

    for (int done = 0; done < numSamples;)
    {
        const int piece = std::min(numSamples - done, pieceLimit);
        float* const mix[] = {output.getWritePointer(0, outputStart + done),
                              output.getWritePointer(1, outputStart + done)};

        auto pieceContext = context;
        pieceContext.fade = context.fade.from(done);
        pieceContext.inputOffset = context.inputOffset + done;
        pieceContext.crossfadeDone = std::min(context.crossfadeLength, context.crossfadeDone + done);
        const juce::int64 pieceStart = context.timelineRunning ? timelineStart + done : timelineStart;

        renderPiece(arrangement, state, pieceStart, mix, piece, pieceContext);
        done += piece;
    }
}

void Mixer::renderPiece(const Arrangement& arrangement, MixState& state, juce::int64 timelineStart,
                        float* const* mix, int numSamples, const MixContext& context) noexcept
{
    for (int channel = 0; channel < state.busInputs.getNumChannels(); ++channel)
        state.busInputs.clear(channel, 0, numSamples);

    for (std::size_t i = 0; i < arrangement.tracks.size(); ++i)
    {
        const auto& track = arrangement.tracks[i];
        const ArrangementTrack* before =
            context.previous != nullptr ? findTrack(*context.previous, track.id, i) : nullptr;

        if (!renderTrack(arrangement, track, state.tracks[i], timelineStart, numSamples, context,
                         before, arrangement, state, false))
            continue;

        // A plain add, never a fused multiply-add, so that rounding is the same
        // whether a sample ends up in a vectorised part or not.
        juce::FloatVectorOperations::add(mix[0], scratch.getReadPointer(0), numSamples);
        juce::FloatVectorOperations::add(mix[1], scratch.getReadPointer(1), numSamples);
    }

    if (context.previous != nullptr && context.previousState != nullptr)
        renderVanishedTracks(arrangement, state, timelineStart, mix, numSamples, context);

    if (!arrangement.buses.empty())
        renderBuses(arrangement, state, timelineStart, mix, numSamples, context);

    state.master.apply(mix, 2, numSamples);

    if (!arrangement.masterInserts.isEmpty())
        processChain(arrangement.masterInserts, arrangement, mix, numSamples, timelineStart,
                     context);
}

void Mixer::renderVanishedTracks(const Arrangement& arrangement, MixState& state,
                                 juce::int64 timelineStart, float* const* mix, int numSamples,
                                 const MixContext& context) noexcept
{
    const auto& previous = *context.previous;
    const int fading = std::min(numSamples, context.crossfadeLength - context.crossfadeDone);
    if (fading <= 0)
        return;

    for (std::size_t i = 0; i < previous.tracks.size(); ++i)
    {
        const auto& track = previous.tracks[i];
        if (findTrack(arrangement, track.id, i) != nullptr)
            continue;

        // A track that is gone fades out, effects and sends and all, as the new mix
        // fades in.
        if (!renderTrack(previous, track, context.previousState->tracks[i], timelineStart, fading,
                         context, nullptr, arrangement, state, true))
            continue;

        juce::FloatVectorOperations::add(mix[0], scratch.getReadPointer(0), fading);
        juce::FloatVectorOperations::add(mix[1], scratch.getReadPointer(1), fading);
    }
}

bool Mixer::renderTrack(const Arrangement& arrangement, const ArrangementTrack& track,
                        MixState::TrackGains& gains, juce::int64 timelineStart, int numSamples,
                        const MixContext& context, const ArrangementTrack* before,
                        const Arrangement& busOwner, MixState& busState, bool vanishing) noexcept
{
    float* const channels[] = {scratch.getWritePointer(0), scratch.getWritePointer(1)};

    // Monitoring: offline renders never hear an input.
    const bool hasInput = context.inputs != nullptr && track.numInputChannels > 0;
    const bool monitors = hasInput && (track.monitoring == InputMonitoring::on ||
                                       (track.monitoring == InputMonitoring::automatic &&
                                        track.armed && context.autoMonitorsInput));
    gains.timeline.setTarget(monitors ? 0.0f : 1.0f, monitorRampLength);
    gains.input.setTarget(monitors ? 1.0f : 0.0f, monitorRampLength);

    const bool playsTimeline = context.timelineRunning &&
                               (gains.timeline.isRamping() || gains.timeline.getTarget() > 0.0f);
    const bool playsInput = hasInput && (gains.input.isRamping() || gains.input.getTarget() > 0.0f);

    const bool unheard = !gains.left.isRamping() && !gains.right.isRamping() &&
                         juce::exactlyEqual(gains.left.getTarget(), 0.0f) &&
                         juce::exactlyEqual(gains.right.getTarget(), 0.0f);
    const bool hasEffects = !track.inserts.isEmpty();

    bool sendsBeforeFader = false;
    for (std::size_t k = 0; k < track.sends.size(); ++k)
        sendsBeforeFader = sendsBeforeFader ||
                           (track.sends[k].preFader &&
                            (gains.sends[k].isRamping() || gains.sends[k].getTarget() > 0.0f));

    // A silent track without effects reads nothing; its gains still move on, as if
    // it had played. With effects it runs on: they may still be ringing.
    if (!hasEffects && ((unheard && !sendsBeforeFader) || (!playsTimeline && !playsInput)))
    {
        for (auto* ramp : {&gains.left, &gains.right, &gains.timeline, &gains.input})
            ramp->skip(numSamples);
        for (auto& send : gains.sends)
            send.skip(numSamples);
        return false;
    }

    juce::FloatVectorOperations::clear(channels[0], numSamples);
    juce::FloatVectorOperations::clear(channels[1], numSamples);

    if (playsTimeline)
    {
        readClips(track, timelineStart, numSamples, scratch);
        if (context.previous != nullptr && !vanishing &&
            (before == nullptr || before->layoutHash != track.layoutHash))
            crossfadeClips(before, timelineStart, numSamples, context);
        context.fade.apply(channels[0], numSamples);
        context.fade.apply(channels[1], numSamples);
        gains.timeline.apply(channels, 2, numSamples);
    }
    else
    {
        gains.timeline.skip(numSamples);
    }

    if (playsInput)
    {
        readInput(track, context, numSamples);
        float* const input[] = {inputScratch.getWritePointer(0), inputScratch.getWritePointer(1)};
        gains.input.apply(input, 2, numSamples);

        // Copied, not added, where the clips are not heard: adding to silence would
        // turn a -0 input sample into +0.
        for (int channel = 0; channel < 2; ++channel)
        {
            if (playsTimeline)
                juce::FloatVectorOperations::add(channels[channel], input[channel], numSamples);
            else
                juce::FloatVectorOperations::copy(channels[channel], input[channel], numSamples);
        }
    }
    else
    {
        gains.input.skip(numSamples);
    }

    if (hasEffects)
        processChain(track.inserts, arrangement, channels, numSamples, timelineStart, context);

    if (vanishing)
    {
        const auto fullLength = static_cast<float>(context.crossfadeLength);
        for (int channel = 0; channel < 2; ++channel)
            for (int i = 0; i < numSamples; ++i)
                channels[channel][i] -= channels[channel][i] *
                                        (static_cast<float>(context.crossfadeDone + i + 1) /
                                         fullLength);
    }

    addSends(track, gains, true, numSamples, busOwner, busState);

    if (unheard)
    {
        gains.left.skip(numSamples);
        gains.right.skip(numSamples);
        for (std::size_t k = 0; k < track.sends.size(); ++k)
            if (!track.sends[k].preFader)
                gains.sends[k].skip(numSamples);
        return false;
    }

    gains.left.apply(channels, 1, numSamples);
    gains.right.apply(channels + 1, 1, numSamples);

    addSends(track, gains, false, numSamples, busOwner, busState);

    if (track.meter != nullptr)
        track.meter->process(scratch, 0, numSamples);

    return true;
}

void Mixer::addSends(const ArrangementTrack& track, MixState::TrackGains& gains, bool preFader,
                     int numSamples, const Arrangement& busOwner, MixState& busState) noexcept
{
    for (std::size_t k = 0; k < track.sends.size(); ++k)
    {
        const auto& send = track.sends[k];
        if (send.preFader != preFader)
            continue;

        auto& ramp = gains.sends[k];
        const int bus = findBus(busOwner, send.bus, send.busIndex);
        const bool silent = !ramp.isRamping() && juce::exactlyEqual(ramp.getTarget(), 0.0f);

        if (bus < 0 || silent || busState.busInputs.getNumChannels() < 2 * (bus + 1))
        {
            ramp.skip(numSamples);
            continue;
        }

        // Multiplied, then added: never a fused multiply-add (see render()).
        float* const sent[] = {sendScratch.getWritePointer(0), sendScratch.getWritePointer(1)};
        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::copy(sent[channel], scratch.getReadPointer(channel),
                                              numSamples);
        ramp.apply(sent, 2, numSamples);

        for (int channel = 0; channel < 2; ++channel)
            juce::FloatVectorOperations::add(busState.busInputs.getWritePointer(2 * bus + channel),
                                             sent[channel], numSamples);
    }
}

void Mixer::renderBuses(const Arrangement& arrangement, MixState& state, juce::int64 timelineStart,
                        float* const* mix, int numSamples, const MixContext& context) noexcept
{
    for (std::size_t i = 0; i < arrangement.buses.size(); ++i)
    {
        const auto& bus = arrangement.buses[i];
        auto& gains = state.buses[i];
        const auto channel = static_cast<int>(2 * i);
        float* const channels[] = {state.busInputs.getWritePointer(channel),
                                   state.busInputs.getWritePointer(channel + 1)};

        if (!bus.inserts.isEmpty())
            processChain(bus.inserts, arrangement, channels, numSamples, timelineStart, context);

        gains.left.apply(channels, 1, numSamples);
        gains.right.apply(channels + 1, 1, numSamples);

        if (bus.meter != nullptr)
        {
            // Refers to the two channels; no allocation for so few channel pointers.
            const juce::AudioBuffer<float> busOutput(channels, 2, numSamples);
            bus.meter->process(busOutput, 0, numSamples);
        }

        juce::FloatVectorOperations::add(mix[0], channels[0], numSamples);
        juce::FloatVectorOperations::add(mix[1], channels[1], numSamples);
    }
}

void Mixer::crossfadeClips(const ArrangementTrack* before, juce::int64 timelineStart,
                           int numSamples, const MixContext& context) noexcept
{
    const int fading = std::min(numSamples, context.crossfadeLength - context.crossfadeDone);
    if (fading <= 0)
        return;

    oldClips.clear(0, 0, fading);
    oldClips.clear(1, 0, fading);
    if (before != nullptr)
        readClips(*before, timelineStart, fading, oldClips);

    const auto fullLength = static_cast<float>(context.crossfadeLength);
    for (int channel = 0; channel < 2; ++channel)
    {
        float* mixed = scratch.getWritePointer(channel);
        const float* old = oldClips.getReadPointer(channel);

        for (int i = 0; i < fading; ++i)
        {
            const float gain = static_cast<float>(context.crossfadeDone + i + 1) / fullLength;
            mixed[i] = old[i] + (mixed[i] - old[i]) * gain;
        }
    }
}

void Mixer::processChain(const EffectChain& chain, const Arrangement& arrangement,
                         float* const* channels, int numSamples, juce::int64 timelineStart,
                         const MixContext& context) noexcept
{
    fx::SlotContext slotContext;
    slotContext.time.tempo = arrangement.tempo;
    slotContext.time.numerator = arrangement.numerator;
    slotContext.time.denominator = arrangement.denominator;
    slotContext.time.position = timelineStart;
    slotContext.time.playing = context.timelineRunning;

    for (const auto& slot : chain.slots)
    {
        if (slot.instance == nullptr)
            continue;

        fx::SlotSettings settings;
        settings.parameters = slot.parameters;
        settings.enabled = slot.enabled;
        settings.revival = slot.revival;
        slot.instance->process(channels, numSamples, settings, slotContext, effectScratch);
    }
}

void Mixer::readInput(const ArrangementTrack& track, const MixContext& context,
                      int numSamples) noexcept
{
    const float* left = context.inputs->channel(track.inputChannel);
    const float* right =
        track.numInputChannels > 1 ? context.inputs->channel(track.inputChannel + 1) : left;

    float* const destination[] = {inputScratch.getWritePointer(0), inputScratch.getWritePointer(1)};
    const float* const sources[] = {left, right};

    for (int channel = 0; channel < 2; ++channel)
    {
        if (sources[channel] != nullptr)
            juce::FloatVectorOperations::copy(destination[channel],
                                              sources[channel] + context.inputOffset, numSamples);
        else
            juce::FloatVectorOperations::clear(destination[channel], numSamples);
    }
}

void Mixer::readClips(const ArrangementTrack& track, juce::int64 timelineStart, int numSamples,
                      juce::AudioBuffer<float>& destination) noexcept
{
    float* const channels[] = {destination.getWritePointer(0), destination.getWritePointer(1)};
    const juce::int64 timelineEnd = timelineStart + numSamples;

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
}

} // namespace daw::engine
