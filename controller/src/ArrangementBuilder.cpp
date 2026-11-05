#include "daw/controller/ArrangementBuilder.h"

#include "daw/controller/EffectInstances.h"
#include "daw/controller/TempoBeatGrid.h"
#include "daw/engine/dsp/PanLaw.h"

#include <algorithm>
#include <cmath>
#include <map>
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

std::vector<model::Effect> effectsOf(const model::EffectList& list)
{
    std::vector<model::Effect> effects;
    for (int i = 0; i < list.getNumEffects(); ++i)
        effects.push_back(list.getEffect(i));
    return effects;
}

// An owner's effects as the engine plays them, with their automation if it reads.
engine::EffectChain makeChain(const ChainOwner& owner, const std::vector<model::Effect>& effects,
                              const model::Automation& automation, bool reads,
                              double sampleRate, const EffectLookup& lookup,
                              const SampleLookup& samples = {})
{
    std::vector<engine::ChainSlot> slots;
    if (lookup)
    {
        slots = lookup(owner, effects);
    }
    else
    {
        for (const auto& effect : effects)
        {
            engine::ChainSlot slot;
            slot.instance = EffectInstances::makeInstance(effect.getType(), sampleRate, 512, false);
            slots.push_back(std::move(slot));
        }
    }

    engine::EffectChain chain;
    for (std::size_t i = 0; i < effects.size() && i < slots.size(); ++i)
    {
        auto& slot = slots[i];
        if (slot.instance == nullptr)
            continue;

        const auto& effect = effects[i];
        const auto& parameters = slot.instance->getDescription().parameters;
        slot.enabled = !effect.isBypassed();
        slot.parameters.clear();
        slot.automation.clear();

        slot.samples.clear();
        slot.samplePointers.clear();
        for (const auto& key : slot.instance->getDescription().sampleSlots)
        {
            const auto file = effect.getFile(key);
            auto sample = samples && file != juce::File() ? samples(file) : nullptr;
            slot.samplePointers.push_back(sample.get());
            slot.samples.push_back(std::move(sample));
        }

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

struct Interval
{
    model::Ticks start = 0;
    model::Ticks end = 0;
};

// The parts of [start, end) that no interval of covered (sorted, disjoint) covers.
std::vector<Interval> uncovered(model::Ticks start, model::Ticks end,
                                const std::vector<Interval>& covered)
{
    std::vector<Interval> parts;
    model::Ticks position = start;
    for (const auto& interval : covered)
    {
        if (interval.end <= position)
            continue;
        if (interval.start >= end)
            break;
        if (interval.start > position)
            parts.push_back({position, interval.start});
        position = std::max(position, interval.end);
        if (position >= end)
            break;
    }
    if (position < end)
        parts.push_back({position, end});
    return parts;
}

void cover(std::vector<Interval>& covered, model::Ticks start, model::Ticks end)
{
    covered.push_back({start, end});
    std::sort(covered.begin(), covered.end(),
              [](const Interval& a, const Interval& b) { return a.start < b.start; });
    std::vector<Interval> merged;
    for (const auto& interval : covered)
    {
        if (!merged.empty() && interval.start <= merged.back().end)
            merged.back().end = std::max(merged.back().end, interval.end);
        else
            merged.push_back(interval);
    }
    covered = std::move(merged);
}

engine::midi::EventType toEngine(model::ControllerType::Kind kind)
{
    switch (kind)
    {
    case model::ControllerType::Kind::pitchBend:
        return engine::midi::EventType::pitchBend;
    case model::ControllerType::Kind::channelPressure:
        return engine::midi::EventType::channelPressure;
    case model::ControllerType::Kind::controlChange:
        break;
    }
    return engine::midi::EventType::controller;
}

float normalisedValue(const model::ControllerType& type, int value)
{
    return type.kind == model::ControllerType::Kind::pitchBend ? engine::midi::fromPitchBend(value)
                                                              : engine::midi::fromSevenBit(value);
}

// A MIDI track's clips as notes and controller lanes in samples: each clip where the
// clips above it leave it visible.
void addMidiContent(const model::Track& track, double sampleRate, engine::ArrangementTrack& out,
                    juce::int64& length)
{
    const auto toSamples = [sampleRate](model::Ticks ticks) {
        return model::ticksToSamples(ticks, sampleRate);
    };
    const auto channel = static_cast<std::uint8_t>(track.getMidiChannel() - 1);

    struct Step
    {
        juce::int64 position = 0;
        int order = 0; // 0: a clip's end going back to rest; 1: anything else
        float value = 0.0f;
    };
    std::map<model::ControllerType, std::vector<Step>> steps;

    std::vector<Interval> covered;
    for (int c = track.getNumClips() - 1; c >= 0; --c)
    {
        const auto clip = track.getClip(c);
        const auto end = clip.getEnd();
        if (!clip.isMidi() || !end)
            continue;

        const auto parts = uncovered(clip.getPosition(), *end, covered);
        cover(covered, clip.getPosition(), *end);
        length = std::max(length, toSamples(*end));

        const model::MidiClip midi(clip.getState());
        const auto origin = clip.getPosition() - clip.getOffset();
        const auto notes = midi.getNotes();
        const auto events = midi.getControllerEvents();

        for (const auto& part : parts)
        {
            for (const auto& note : notes)
            {
                const auto start = origin + note.start;
                if (start < part.start || start >= part.end)
                    continue;

                engine::ArrangementNote played;
                played.start = toSamples(start);
                played.end = std::max(played.start + 1,
                                      toSamples(std::min(origin + note.getEnd(), part.end)));
                played.key = static_cast<std::uint8_t>(note.key);
                played.channel = channel;
                played.velocity = engine::midi::fromSevenBit(note.velocity);
                played.identity = noteIdentity(clip.getId(), note.id);
                out.notes.push_back(played);
            }

            for (const auto& type : midi.getControllerTypes())
            {
                auto& lane = steps[type];
                int value = type.restValue();
                for (const auto& event : events)
                {
                    if (event.type != type)
                        continue;
                    const auto at = origin + event.position;
                    if (at <= part.start)
                    {
                        value = event.value;
                        continue;
                    }
                    if (at >= part.end)
                        break;
                    lane.push_back({toSamples(at), 1, normalisedValue(type, event.value)});
                }
                lane.push_back({toSamples(part.start), 1, normalisedValue(type, value)});
                lane.push_back({toSamples(part.end), 0, normalisedValue(type, type.restValue())});
            }
        }
    }

    std::stable_sort(out.notes.begin(), out.notes.end(),
                     [](const engine::ArrangementNote& a, const engine::ArrangementNote& b) {
                         return a.start < b.start;
                     });

    for (auto& [type, lane] : steps)
    {
        std::stable_sort(lane.begin(), lane.end(), [](const Step& a, const Step& b) {
            return a.position != b.position ? a.position < b.position : a.order < b.order;
        });

        engine::ControllerLane result;
        result.type = toEngine(type.kind);
        result.number = static_cast<std::uint8_t>(type.number);
        result.channel = channel;
        float current = normalisedValue(type, type.restValue());
        for (std::size_t i = 0; i < lane.size(); ++i)
        {
            // At one position the last step holds.
            if (i + 1 < lane.size() && lane[i + 1].position == lane[i].position)
                continue;
            if (juce::exactlyEqual(lane[i].value, current))
                continue;
            current = lane[i].value;
            result.steps.push_back({lane[i].position, current});
        }
        if (!result.steps.empty())
            out.controllers.push_back(std::move(result));
    }
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

std::uint64_t noteIdentity(model::ObjectId clipId, std::int32_t noteId) noexcept
{
    return (static_cast<std::uint64_t>(clipId) << 32) ^ static_cast<std::uint32_t>(noteId);
}

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
                                                      const EffectLookup& effects,
                                                      const MidiDeviceLookup& midiDevices,
                                                      const SampleLookup& samples)
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
        arrangementBus.inserts = makeChain(ChainOwner::bus(bus.getId()), effectsOf(bus.getEffects()),
                                           automation, reads, sampleRate, effects);
        if (meters)
            arrangementBus.meter = meters(bus.getId());
        arrangement->buses.push_back(std::move(arrangementBus));
    }

    const auto masterAutomation = project.getMasterAutomation();
    const bool masterReads = masterAutomation.getMode() == model::AutomationMode::read;
    arrangement->masterInserts = makeChain(ChainOwner::master(),
                                           effectsOf(project.getMasterEffects()), masterAutomation,
                                           masterReads, sampleRate, effects);
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
        arrangementTrack.heard = audible;
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
        auto chain = effectsOf(track.getEffects());
        if (const auto instrument = track.isMidi() ? track.getInstrument() : std::nullopt)
            chain.insert(chain.begin(), *instrument);
        arrangementTrack.inserts = makeChain(ChainOwner::track(track.getId()), chain, automation,
                                             reads, sampleRate, effects, samples);

        if (track.isMidi())
        {
            arrangementTrack.playsNotes = true;
            const auto midiInput = track.getMidiInput();
            arrangementTrack.midiInput.channel = midiInput.channel - 1;
            if (midiInput.device.isNotEmpty() && midiDevices)
            {
                const int device = midiDevices(midiInput.device);
                arrangementTrack.midiInput.device =
                    device >= 0 ? device : engine::midi::InputFilter::noDevice;
            }
            if (audible)
                addMidiContent(track, sampleRate, arrangementTrack, arrangement->length);
            else
                for (int c = 0; c < track.getNumClips(); ++c)
                    if (const auto end = track.getClip(c).getEnd())
                        arrangement->length = std::max(arrangement->length, toSamples(*end));
        }

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
            if (clip.isMidi())
                continue;
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
