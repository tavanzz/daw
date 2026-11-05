#include "daw/engine/fx/DrumSampler.h"

#include "daw/engine/fx/Sampler.h"

#include <algorithm>
#include <cmath>

namespace daw::engine::fx
{

namespace
{

ParameterInfo linear(const juce::String& id, const juce::String& name, const char* unit,
                     float minimum, float maximum, float defaultValue)
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

// General MIDI's names for the default keys.
constexpr std::array<const char*, DrumSampler::numPads> padNames{
    "Kick",     "Side Stick", "Snare",    "Clap",      "Snare 2",    "Low Floor Tom",
    "Closed Hat", "High Floor Tom", "Pedal Hat", "Low Tom", "Open Hat", "Low-Mid Tom",
    "High-Mid Tom", "Crash", "High Tom", "Ride"};

} // namespace

juce::String DrumSampler::slotOf(int pad)
{
    return "pad" + juce::String(pad + 1);
}

DrumSampler::DrumSampler()
{
    description.type = typeId;
    description.name = "Drum Sampler";
    description.isInstrument = true;

    juce::StringArray keys;
    for (int key = 0; key < 128; ++key)
        keys.add(keyName(key));
    juce::StringArray groups{"None"};
    for (int group = 1; group <= numChokeGroups; ++group)
        groups.add(juce::String(group));

    auto& p = description.parameters;
    for (int pad = 0; pad < numPads; ++pad)
    {
        description.sampleSlots.push_back(slotOf(pad));
        const auto prefix = "pad" + juce::String(pad + 1);
        const auto name = "Pad " + juce::String(pad + 1);
        const auto group = name + " (" + padNames[static_cast<std::size_t>(pad)] + ")";
        const int key = firstKey + pad;
        const bool hat = key == 42 || key == 44 || key == 46;

        std::array<ParameterInfo, parametersPerPad> padParameters{
            choiceParameter(prefix + "Key", name + " Key", keys, key),
            linear(prefix + "Level", name + " Level", "dB", -48.0f, 6.0f, 0.0f),
            linear(prefix + "Pan", name + " Pan", "%", -100.0f, 100.0f, 0.0f),
            linear(prefix + "Tune", name + " Tune", "st", -24.0f, 24.0f, 0.0f),
            linear(prefix + "Decay", name + " Decay", "ms", 10.0f, 10000.0f, 2000.0f),
            choiceParameter(prefix + "Choke", name + " Choke", groups, hat ? 1 : 0)};
        padParameters[padDecay].scale = ParameterScale::logarithmic;
        for (auto& info : padParameters)
        {
            info.group = group;
            p.push_back(std::move(info));
        }
    }

    auto velocityInfo = linear("velocity", "Velocity to Level", "%", 0.0f, 100.0f, 80.0f);
    juce::StringArray counts;
    for (int count = 1; count <= maximumVoices; ++count)
        counts.add(juce::String(count));
    auto polyphonyInfo = choiceParameter("polyphony", "Polyphony", counts, maximumVoices - 1);
    auto levelInfo = linear("level", "Level", "dB", -24.0f, 6.0f, 0.0f);
    for (auto* info : {&velocityInfo, &polyphonyInfo, &levelInfo})
    {
        info->group = "Kit";
        p.push_back(std::move(*info));
    }

    for (std::size_t i = 0; i < p.size(); ++i)
        values[i] = p[i].defaultValue;
}

const EffectDescription& DrumSampler::getDescription() const noexcept
{
    return description;
}

double DrumSampler::getTailSeconds(std::span<const float> parameterValues) const
{
    double longest = 0.0;
    for (int pad = 0; pad < numPads; ++pad)
    {
        const auto index = static_cast<std::size_t>(parameterOf(pad, padDecay));
        if (index < parameterValues.size())
            longest = std::max(longest, static_cast<double>(parameterValues[index]));
    }
    return longest / 1000.0 * 100.0 / 60.0 + 0.01;
}

void DrumSampler::readParameters(const ProcessBlock& block, int sample) noexcept
{
    const int count = std::min(block.parameters.size(), static_cast<int>(numParameters));
    for (int p = 0; p < count; ++p)
        values[static_cast<std::size_t>(p)] = block.parameters.at(p, sample);
}

bool DrumSampler::zoneFor(int key, Zone& zone) const noexcept
{
    const auto value = [this](int pad, PadParameter parameter) {
        return values[static_cast<std::size_t>(parameterOf(pad, parameter))];
    };

    for (int pad = 0; pad < numPads; ++pad)
    {
        if (static_cast<int>(std::lround(value(pad, padKey))) != key)
            continue;
        zone = Zone{};
        zone.slot = pad;
        zone.rootKey = key;
        zone.tune = value(pad, padTune);
        zone.gain = std::pow(10.0f, value(pad, padLevel) / 20.0f);
        zone.pan = value(pad, padPan) / 100.0f;
        zone.oneShot = true;
        zone.chokeGroup = static_cast<int>(std::lround(value(pad, padChoke)));
        zone.attack = 0.0005; // just enough not to click
        zone.decay = value(pad, padDecay) / 1000.0;
        zone.sustain = 0.0;
        zone.release = 0.05;
        zone.velocitySensitivity = values[velocity] / 100.0f;
        return true;
    }
    return false;
}

int DrumSampler::polyphonyNow() const noexcept
{
    return static_cast<int>(values[polyphony]) + 1;
}

float DrumSampler::levelNow() const noexcept
{
    return std::pow(10.0f, values[level] / 20.0f);
}

} // namespace daw::engine::fx
