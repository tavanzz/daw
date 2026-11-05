#include "daw/engine/fx/Sampler.h"

#include <algorithm>
#include <cmath>

namespace daw::engine::fx
{

namespace
{

ParameterInfo linear(const char* id, const char* name, const char* unit, float minimum,
                     float maximum, float defaultValue)
{
    ParameterInfo info;
    info.id = id;
    info.name = name;
    info.unit = unit;
    info.minimum = minimum;
    info.maximum = maximum;
    info.defaultValue = defaultValue;
    return info;
}

ParameterInfo logarithmic(const char* id, const char* name, const char* unit, float minimum,
                          float maximum, float defaultValue)
{
    auto info = linear(id, name, unit, minimum, maximum, defaultValue);
    info.scale = ParameterScale::logarithmic;
    return info;
}

juce::StringArray numbers(int from, int to)
{
    juce::StringArray names;
    for (int value = from; value <= to; ++value)
        names.add(juce::String(value));
    return names;
}

} // namespace

juce::String keyName(int key)
{
    static const char* const names[] = {"C", "C#", "D", "D#", "E", "F", "F#", "G", "G#", "A", "A#", "B"};
    const int clamped = std::clamp(key, 0, 127);
    return juce::String(names[clamped % 12]) + juce::String(clamped / 12 - 2);
}

Sampler::Sampler()
{
    description.type = typeId;
    description.name = "Sampler";
    description.isInstrument = true;
    description.sampleSlots = {sampleSlot};

    juce::StringArray keys;
    for (int key = 0; key < 128; ++key)
        keys.add(keyName(key));

    auto& p = description.parameters;
    p.resize(numParameters);
    const auto set = [&p](Parameter index, ParameterInfo info, const char* group) {
        info.group = group;
        p[static_cast<std::size_t>(index)] = std::move(info);
    };
    set(rootKey, choiceParameter("rootKey", "Root Key", keys, 60), "Pitch");
    set(transpose, linear("transpose", "Transpose", "st", -48.0f, 48.0f, 0.0f), "Pitch");
    set(fine, linear("fine", "Fine", "ct", -100.0f, 100.0f, 0.0f), "Pitch");
    set(start, linear("start", "Start", "%", 0.0f, 100.0f, 0.0f), "Sample");
    set(loop, choiceParameter("loop", "Loop", {"Off", "Forward", "Ping-pong"}, 0), "Sample");
    set(loopStart, linear("loopStart", "Loop Start", "%", 0.0f, 100.0f, 0.0f), "Sample");
    set(loopEnd, linear("loopEnd", "Loop End", "%", 0.0f, 100.0f, 100.0f), "Sample");
    set(crossfade, linear("crossfade", "Loop Crossfade", "ms", 0.0f, 500.0f, 10.0f), "Sample");
    set(attack, logarithmic("attack", "Attack", "ms", 0.1f, 10000.0f, 1.0f), "Envelope");
    set(decay, logarithmic("decay", "Decay", "ms", 1.0f, 20000.0f, 500.0f), "Envelope");
    set(sustain, linear("sustain", "Sustain", "%", 0.0f, 100.0f, 100.0f), "Envelope");
    set(release, logarithmic("release", "Release", "ms", 1.0f, 20000.0f, 200.0f), "Envelope");
    set(velocity, linear("velocity", "Velocity to Level", "%", 0.0f, 100.0f, 70.0f), "Envelope");
    set(polyphony, choiceParameter("polyphony", "Polyphony", numbers(1, maximumVoices), 15), "Voices");
    set(bendRange, choiceParameter("bendRange", "Bend Range", numbers(0, 24), 2), "Voices");
    set(level, linear("level", "Level", "dB", -24.0f, 6.0f, 0.0f), "Voices");

    for (std::size_t i = 0; i < p.size(); ++i)
        values[i] = p[i].defaultValue;
}

const EffectDescription& Sampler::getDescription() const noexcept
{
    return description;
}

double Sampler::getTailSeconds(std::span<const float> parameterValues) const
{
    const auto releaseMs = parameterValues.size() > static_cast<std::size_t>(release)
                               ? static_cast<double>(parameterValues[static_cast<std::size_t>(release)])
                               : 200.0;
    return releaseMs / 1000.0 * 100.0 / 60.0 + 0.01;
}

void Sampler::readParameters(const ProcessBlock& block, int sample) noexcept
{
    const int count = std::min(block.parameters.size(), static_cast<int>(numParameters));
    for (int p = 0; p < count; ++p)
        values[static_cast<std::size_t>(p)] = block.parameters.at(p, sample);
}

bool Sampler::zoneFor(int, Zone& zone) const noexcept
{
    const auto value = [this](Parameter p) { return values[static_cast<std::size_t>(p)]; };
    zone.slot = 0;
    zone.rootKey = std::round(value(rootKey));
    zone.tune = value(transpose) + value(fine) / 100.0;
    zone.start = value(start) / 100.0;
    zone.loop = static_cast<Loop>(std::clamp(static_cast<int>(value(loop)), 0, 2));
    zone.loopStart = value(loopStart) / 100.0;
    zone.loopEnd = value(loopEnd) / 100.0;
    zone.crossfadeSeconds = value(crossfade) / 1000.0;
    zone.attack = value(attack) / 1000.0;
    zone.decay = value(decay) / 1000.0;
    zone.sustain = value(sustain) / 100.0;
    zone.release = value(release) / 1000.0;
    zone.velocitySensitivity = value(velocity) / 100.0f;
    return true;
}

int Sampler::polyphonyNow() const noexcept
{
    return static_cast<int>(values[polyphony]) + 1;
}

double Sampler::bendRangeNow() const noexcept
{
    return std::round(values[bendRange]);
}

float Sampler::levelNow() const noexcept
{
    return std::pow(10.0f, values[level] / 20.0f);
}

} // namespace daw::engine::fx
