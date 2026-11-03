#include "daw/controller/ArrangementBuilder.h"

#include "daw/controller/EffectInstances.h"
#include "daw/controller/TempoBeatGrid.h"
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

std::shared_ptr<const engine::AutomationCurve> curveFor(const model::Automation& automation,
                                                        const model::AutomationTarget& target,
                                                        double sampleRate)
{
    const int index = automation.indexOfLane(target);
    return index >= 0 ? makeCurve(automation.getLane(index), sampleRate) : nullptr;
}

// An owner's effects as the engine plays them, with their automation if it reads.
engine::EffectChain makeChain(const ChainOwner& owner, const model::EffectList& effects,
                              const model::Automation& automation, bool reads,
                              double sampleRate, const EffectLookup& lookup)
{
    std::vector<engine::ChainSlot> slots;
    if (lookup)
    {
        slots = lookup(owner, effects);
    }
    else
    {
        for (int i = 0; i < effects.getNumEffects(); ++i)
        {
            engine::ChainSlot slot;
            slot.instance = EffectInstances::makeInstance(effects.getEffect(i).getType(),
                                                          sampleRate, 512, false);
            slots.push_back(std::move(slot));
        }
    }

    engine::EffectChain chain;
    for (int i = 0; i < effects.getNumEffects() && static_cast<std::size_t>(i) < slots.size(); ++i)
    {
        auto& slot = slots[static_cast<std::size_t>(i)];
        if (slot.instance == nullptr)
            continue;

        const auto effect = effects.getEffect(i);
        const auto& parameters = slot.instance->getDescription().parameters;
        slot.enabled = !effect.isBypassed();
        slot.parameters.clear();
        slot.automation.clear();

        for (std::size_t p = 0; p < parameters.size(); ++p)
        {
            const auto& info = parameters[p];
            const auto value = static_cast<float>(
                effect.getParameter(info.id).value_or(static_cast<double>(info.defaultValue)));
            slot.parameters.push_back(info.toNormalised(value));

            if (!reads || !info.automatable)
                continue;
            if (auto curve = curveFor(automation,
                                      model::AutomationTarget::effectParameter(effect.getId(), info.id),
                                      sampleRate))
            {
                slot.automation.resize(parameters.size());
                slot.automation[p] = std::move(curve);
            }
        }

        chain.slots.push_back(std::move(slot));
    }
    return chain;
}

engine::GainAutomation makeGainAutomation(double volume, double pan,
                                          const model::Automation& automation, bool active,
                                          double sampleRate)
{
    engine::GainAutomation gains;
    gains.volume = static_cast<float>(volume);
    gains.pan = static_cast<float>(pan);
    if (active && automation.getMode() == model::AutomationMode::read)
    {
        gains.volumeCurve = curveFor(automation, model::AutomationTarget::volume(), sampleRate);
        gains.panCurve = curveFor(automation, model::AutomationTarget::pan(), sampleRate);
    }
    return gains;
}

} // namespace

std::shared_ptr<const engine::AutomationCurve> makeCurve(const model::AutomationLane& lane,
                                                         double sampleRate)
{
    if (lane.getNumPoints() == 0)
        return nullptr;

    std::vector<engine::AutomationPoint> points;
    for (int i = 0; i < lane.getNumPoints(); ++i)
    {
        const auto point = lane.getPoint(i);
        points.push_back({model::ticksToSamples(point.position, sampleRate),
                          static_cast<float>(point.value), static_cast<float>(point.curve)});
    }
    return std::make_shared<const engine::AutomationCurve>(std::move(points));
}

std::unique_ptr<engine::Arrangement> buildArrangement(const model::Project& project,
                                                      double sampleRate,
                                                      const SourceLookup& sources,
                                                      const MeterLookup& meters,
                                                      const EffectLookup& effects)
{
    jassert(sampleRate > 0.0);

    auto arrangement = std::make_unique<engine::Arrangement>();
    arrangement->sampleRate = sampleRate;
    arrangement->masterGain = static_cast<float>(project.getMasterVolume());
    arrangement->beats = std::make_shared<TempoBeatGrid>(project.getTempoMap(), sampleRate);
    arrangement->metronomeEnabled = project.isMetronomeEnabled();
    arrangement->metronomeGain = static_cast<float>(project.getMetronomeVolume());

    const auto tempoMap = project.getTempoMap();
    arrangement->tempo = tempoMap.getTempo();
    arrangement->numerator = tempoMap.getTimeSignature().numerator;
    arrangement->denominator = tempoMap.getTimeSignature().denominator;

    // Buses first: sends point at them.
    for (int busIndex = 0; busIndex < project.getNumBuses(); ++busIndex)
    {
        const auto bus = project.getBus(busIndex);
        const auto automation = bus.getAutomation();
        const bool reads = automation.getMode() == model::AutomationMode::read;

        engine::ArrangementBus arrangementBus;
        arrangementBus.id = bus.getId();
        if (!bus.isMuted())
        {
            const auto pan = engine::dsp::panGains(bus.getPan());
            arrangementBus.leftGain = static_cast<float>(bus.getVolume() * pan.left);
            arrangementBus.rightGain = static_cast<float>(bus.getVolume() * pan.right);
        }
        else
        {
            arrangementBus.leftGain = 0.0f;
            arrangementBus.rightGain = 0.0f;
        }
        arrangementBus.automation =
            makeGainAutomation(bus.getVolume(), bus.getPan(), automation, !bus.isMuted(), sampleRate);
        arrangementBus.inserts = makeChain(ChainOwner::bus(bus.getId()), bus.getEffects(),
                                           automation, reads, sampleRate, effects);
        if (meters)
            arrangementBus.meter = meters(bus.getId());
        arrangement->buses.push_back(std::move(arrangementBus));
    }

    const auto masterAutomation = project.getMasterAutomation();
    const bool masterReads = masterAutomation.getMode() == model::AutomationMode::read;
    arrangement->masterInserts = makeChain(ChainOwner::master(), project.getMasterEffects(),
                                           masterAutomation, masterReads, sampleRate, effects);
    if (masterReads)
        arrangement->masterCurve =
            curveFor(masterAutomation, model::AutomationTarget::volume(), sampleRate);

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

        const bool audible = project.isTrackAudible(trackIndex);
        if (audible)
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

        const auto automation = track.getAutomation();
        const bool reads = automation.getMode() == model::AutomationMode::read;
        arrangementTrack.automation =
            makeGainAutomation(track.getVolume(), track.getPan(), automation, audible, sampleRate);
        arrangementTrack.inserts = makeChain(ChainOwner::track(track.getId()), track.getEffects(),
                                             automation, reads, sampleRate, effects);

        // A track that is not heard sends nothing.
        for (int sendIndex = 0; sendIndex < track.getNumSends(); ++sendIndex)
        {
            const auto send = track.getSend(sendIndex);
            const int busIndex = project.indexOfBus(send.getBus());
            if (busIndex < 0)
                continue;

            engine::ArrangementSend arrangementSend;
            arrangementSend.id = send.getId();
            arrangementSend.bus = send.getBus();
            arrangementSend.busIndex = busIndex;
            arrangementSend.gain = audible ? static_cast<float>(send.getLevel()) : 0.0f;
            arrangementSend.preFader = send.isPreFader();
            if (audible && reads)
                arrangementSend.levelCurve = curveFor(
                    automation, model::AutomationTarget::sendLevel(send.getId()), sampleRate);
            arrangementTrack.sends.push_back(std::move(arrangementSend));
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
