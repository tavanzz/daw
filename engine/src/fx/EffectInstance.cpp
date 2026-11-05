#include "daw/engine/fx/EffectInstance.h"

#include <algorithm>
#include <array>
#include <cmath>

namespace daw::engine::fx
{

int EffectDescription::indexOf(const juce::String& parameterId) const noexcept
{
    for (std::size_t i = 0; i < parameters.size(); ++i)
        if (parameters[i].id == parameterId)
            return static_cast<int>(i);
    return -1;
}

void EffectScratch::prepare(int maximumBlockSize, int eventCapacity)
{
    jassert(maximumBlockSize > 0);
    blockSize = maximumBlockSize;
    dry.setSize(EffectInstance::numChannels, maximumBlockSize);
    values.assign(static_cast<std::size_t>(maximumChangingParameters * maximumBlockSize), 0.0f);
    gains.assign(static_cast<std::size_t>(maximumBlockSize), 0.0f);

    // A piece holds some of the events it is given plus a note-off for every held
    // note, and accepts at least as many notes as the buffer they come from.
    events.prepare(eventCapacity + midi::EventBuffer::maximumHeldNotes);
}

bool allFinite(const float* const* channels, int numChannels, int numSamples) noexcept
{
    // x * 0 is 0 for every finite x and NaN for a NaN or an infinity, so one NaN
    // makes the sum NaN. No branch per sample: the loop vectorises.
    float sum = 0.0f;
    for (int channel = 0; channel < numChannels; ++channel)
        for (int i = 0; i < numSamples; ++i)
            sum += channels[channel][i] * 0.0f;
    return !std::isnan(sum);
}

EffectInstance::EffectInstance(std::unique_ptr<Effect> effectToHost, const ProcessSetup& newSetup,
                               bool fadeIn)
    : effect(std::move(effectToHost))
    , setup(newSetup)
    , fadesIn(fadeIn)
{
    jassert(effect != nullptr);
    setup.numChannels = numChannels;
    effect->prepare(setup);
    instrument = effect->getDescription().isInstrument;
    latency = std::max(0, effect->getLatency());

    fadeLength = std::max(1, static_cast<int>(std::lround(fadeSeconds * setup.sampleRate)));
    rampLength = std::max(1, static_cast<int>(std::lround(rampSeconds * setup.sampleRate)));

    const auto& parameters = effect->getDescription().parameters;
    for (const auto& parameter : parameters)
    {
        mappings.emplace_back(parameter);
        defaults.push_back(parameter.toNormalised(parameter.defaultValue));
        lastValues.push_back(parameter.clamp(parameter.defaultValue));
    }
    ramps.resize(parameters.size());
    blends.assign(parameters.size(), dsp::GainRamp(0.0f));
    lastNormalised.assign(parameters.size(), -1.0f);
    changing.assign(parameters.size(), nullptr);

    dryDelay.prepare(numChannels, latency);
    dryDelay.setDelay(latency);
}

void EffectInstance::process(float* const* channels, int numSamples, const SlotSettings& settings,
                             const SlotContext& context, EffectScratch& scratch) noexcept
{
    const int pieceLimit = std::min(setup.maximumBlockSize, scratch.blockSize);
    jassert(pieceLimit > 0);

    const auto events = instrument ? context.events : midi::EventSpan{};
    std::size_t first = 0;
    for (int done = 0; done < numSamples;)
    {
        const int piece = std::min(numSamples - done, pieceLimit);

        // The last piece takes whatever is left, so that no note-off is lost to an
        // offset out of range.
        std::size_t last = first;
        while (last < events.size() &&
               (done + piece >= numSamples || events[last].offset < done + piece))
            ++last;

        float* const pieceChannels[] = {channels[0] + done, channels[1] + done};
        processPiece(pieceChannels, piece, settings, context, done,
                     events.subspan(first, last - first), scratch);
        first = last;
        done += piece;
    }
}

void EffectInstance::panic() noexcept
{
    if (!instrument)
        return;
    held.clear();
    effect->panic();
}

void EffectInstance::gatherEvents(midi::EventSpan events, int offset, int numSamples,
                                  bool delivering, EffectScratch& scratch) noexcept
{
    auto& buffer = scratch.events;
    buffer.clear();

    // Switched off: every note it holds ends now, as it fades out.
    if (!delivering)
    {
        held.releaseAll(buffer, 0);
        return;
    }

    // A note-off refused (not a note given yet) and, on the same sample, that note's
    // note-on: a source let a note end where it starts, and sorting put the note-off
    // first. The note-on goes too, or the note would never end.
    std::array<midi::Event, 8> refused{};
    int refusedCount = 0;
    int refusedOffset = -1;
    const auto takeRefused = [&](const midi::Event& noteOn) noexcept {
        if (noteOn.offset != refusedOffset)
            return false;
        for (int i = 0; i < refusedCount; ++i)
        {
            const auto& off = refused[static_cast<std::size_t>(i)];
            const bool sameNote = off.noteId >= 0 && noteOn.noteId >= 0
                                      ? off.noteId == noteOn.noteId
                                      : off.number == noteOn.number && off.channel == noteOn.channel;
            if (sameNote)
            {
                refused[static_cast<std::size_t>(i)] = refused[static_cast<std::size_t>(--refusedCount)];
                return true;
            }
        }
        return false;
    };

    for (auto event : events)
    {
        event.offset = std::clamp(event.offset - offset, 0, numSamples - 1);
        switch (event.type)
        {
        case midi::EventType::noteOn:
            if (!takeRefused(event) && buffer.hasRoomForNotes() && held.add(event))
                buffer.add(event);
            break;
        case midi::EventType::noteOff:
            if (held.remove(event))
            {
                buffer.add(event);
            }
            else
            {
                if (event.offset != refusedOffset)
                {
                    refusedOffset = event.offset;
                    refusedCount = 0;
                }
                if (refusedCount < static_cast<int>(refused.size()))
                    refused[static_cast<std::size_t>(refusedCount++)] = event;
            }
            break;
        case midi::EventType::controller:
        case midi::EventType::pitchBend:
        case midi::EventType::channelPressure:
            buffer.add(event);
            break;
        }
    }
}

float EffectInstance::targetOf(const SlotSettings& settings, std::size_t index) const noexcept
{
    if (index >= settings.parameters.size())
        return defaults[index];

    const float target = settings.parameters[index];
    return std::isfinite(target) ? std::clamp(target, 0.0f, 1.0f) : defaults[index];
}

void EffectInstance::start(const SlotSettings& settings, const SlotContext& context) noexcept
{
    started = true;
    revival = settings.revival;

    // Automation that is there from the start applies from the first sample.
    for (std::size_t i = 0; i < ramps.size(); ++i)
    {
        ramps[i].reset(targetOf(settings, i));
        const bool automated = i < context.automation.size() && context.automation[i] != nullptr;
        blends[i].reset(automated ? 1.0f : 0.0f);
    }

    // An effect added while playing fades in; one that was there all along (a
    // project just opened, an export) is in from its first sample.
    wet.reset(settings.enabled && !fadesIn ? 1.0f : 0.0f);
    sleeping = !settings.enabled;
}

void EffectInstance::processPiece(float* const* channels, int numSamples,
                                  const SlotSettings& settings, const SlotContext& context,
                                  int offset, midi::EventSpan events,
                                  EffectScratch& scratch) noexcept
{
    if (!started)
        start(settings, context);

    if (settings.revival != revival)
    {
        revival = settings.revival;
        if (failedHere)
        {
            failedHere = false;
            failed.store(false, std::memory_order_relaxed);
            wet.reset(0.0f); // fades in again from the dry signal
            sleeping = true; // so that it starts from a clean state
        }
    }

    wet.setTarget(settings.enabled && !failedHere ? 1.0f : 0.0f, fadeLength);
    const bool fading = wet.isRamping();
    const bool dryOnly = !fading && juce::exactlyEqual(wet.getTarget(), 0.0f);

    updateParameters(settings, context, offset, numSamples, !dryOnly && !failedHere, scratch);

    float* const dry[] = {scratch.dry.getWritePointer(0), scratch.dry.getWritePointer(1)};
    if (latency > 0)
        dryDelay.process(channels, dry, numChannels, numSamples);

    if (failedHere)
    {
        wet.skip(numSamples);
        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::clear(channels[channel], numSamples);
        return;
    }

    if (instrument)
        gatherEvents(events, offset, numSamples, settings.enabled, scratch);

    if (dryOnly)
    {
        jassert(held.isEmpty());
        sleeping = true;
        if (latency > 0)
            for (int channel = 0; channel < numChannels; ++channel)
                juce::FloatVectorOperations::copy(channels[channel], dry[channel], numSamples);
        return;
    }

    // Back from dry: whatever the effect held from before belongs to another time.
    if (sleeping)
    {
        effect->reset();
        sleeping = false;
    }

    if (fading && latency == 0)
        for (int channel = 0; channel < numChannels; ++channel)
            juce::FloatVectorOperations::copy(dry[channel], channels[channel], numSamples);

    ProcessBlock block;
    block.channels = channels;
    block.numChannels = numChannels;
    block.numSamples = numSamples;
    block.parameters = ParameterValues(lastValues.data(), changing.data(),
                                       static_cast<int>(lastValues.size()));
    block.time = context.time;
    if (block.time.playing)
        block.time.position += offset;
    if (instrument)
        block.events = scratch.events.events();
    block.samples = settings.samples;

    effect->process(block);

    if (!allFinite(channels, numChannels, numSamples))
    {
        wet.skip(numSamples);
        fail(channels, numSamples);
        return;
    }

    if (!fading)
        return;

    float* const gains = scratch.gains.data();
    wet.fill(gains, numSamples);
    for (int channel = 0; channel < numChannels; ++channel)
    {
        float* out = channels[channel];
        const float* in = dry[channel];
        for (int i = 0; i < numSamples; ++i)
            out[i] = in[i] + (out[i] - in[i]) * gains[i];
    }
}

void EffectInstance::fail(float* const* channels, int numSamples) noexcept
{
    failedHere = true;
    failed.store(true, std::memory_order_relaxed);
    effect->reset();
    held.clear();

    for (int channel = 0; channel < numChannels; ++channel)
        juce::FloatVectorOperations::clear(channels[channel], numSamples);
}

void EffectInstance::updateParameters(const SlotSettings& settings, const SlotContext& context,
                                      int offset, int numSamples, bool needValues,
                                      EffectScratch& scratch) noexcept
{
    int lanesUsed = 0;

    for (std::size_t i = 0; i < ramps.size(); ++i)
    {
        const auto& mapping = mappings[i];
        auto& ramp = ramps[i];
        changing[i] = nullptr;

        auto& blend = blends[i];
        const float* automated = i < context.automation.size() ? context.automation[i] : nullptr;
        if (automated != nullptr)
        {
            automated += offset;
            blend.setTarget(1.0f, rampLength);
            const bool blending = blend.isRamping() && !mapping.isChoice();

            float* values = nullptr;
            if (lanesUsed < EffectScratch::maximumChangingParameters &&
                (needValues || blending))
                values = scratch.values.data() + static_cast<std::size_t>(lanesUsed++) *
                                                     static_cast<std::size_t>(scratch.blockSize);

            // Automation is exact: no ramp, once reached. When it stops, the ramp
            // glides on from its last value to the user's.
            float last = automated[numSamples - 1];
            if (blending && values != nullptr)
            {
                float* const amount = scratch.gains.data();
                blend.fill(amount, numSamples);
                ramp.fill(values, numSamples);
                for (int k = 0; k < numSamples; ++k)
                    values[k] += (automated[k] - values[k]) * amount[k];
                last = values[numSamples - 1];
                for (int k = 0; k < numSamples; ++k)
                    values[k] = mapping.toValue(values[k]);
            }
            else
            {
                blend.reset(1.0f);
                if (values != nullptr)
                    for (int k = 0; k < numSamples; ++k)
                        values[k] = mapping.toValue(automated[k]);
            }

            // While blending the ramp goes on underneath (fill() moved it on).
            if (!blending || values == nullptr)
                ramp.reset(last);
            lastNormalised[i] = last;
            lastValues[i] = mapping.toValue(last);
            changing[i] = needValues ? values : nullptr;
            continue;
        }

        blend.reset(0.0f);

        const float target = targetOf(settings, i);
        if (mapping.isChoice())
            ramp.reset(target);
        else
            ramp.setTarget(target, rampLength);

        if (ramp.isRamping() && needValues &&
            lanesUsed < EffectScratch::maximumChangingParameters)
        {
            float* values = scratch.values.data() + static_cast<std::size_t>(lanesUsed++) *
                                                        static_cast<std::size_t>(scratch.blockSize);
            ramp.fill(values, numSamples);
            for (int k = 0; k < numSamples; ++k)
                values[k] = mapping.toValue(values[k]);
            changing[i] = values;
            lastNormalised[i] = ramp.getCurrent();
            lastValues[i] = values[numSamples - 1];
            continue;
        }

        ramp.skip(numSamples);
        const float normalised = ramp.getCurrent();
        if (!juce::exactlyEqual(normalised, lastNormalised[i]))
        {
            lastNormalised[i] = normalised;
            lastValues[i] = mapping.toValue(normalised);
        }
    }
}

} // namespace daw::engine::fx
