#include "daw/engine/fx/Compressor.h"

#include <algorithm>
#include <cmath>

namespace daw::engine::fx
{

namespace
{

constexpr double silenceDb = -200.0;

ParameterInfo parameter(const char* id, const char* name, const char* unit, float minimum,
                        float maximum, float defaultValue,
                        ParameterScale scale = ParameterScale::linear)
{
    ParameterInfo info;
    info.id = id;
    info.name = name;
    info.unit = unit;
    info.minimum = minimum;
    info.maximum = maximum;
    info.defaultValue = defaultValue;
    info.scale = scale;
    return info;
}

double coefficientFor(double milliseconds, double sampleRate) noexcept
{
    return std::exp(-1.0 / (std::max(milliseconds, 0.001) * 0.001 * sampleRate));
}

} // namespace

Compressor::Compressor()
{
    description.type = typeId;
    description.name = "Compressor";
    description.parameters = {
        parameter("threshold", "Threshold", "dB", -60.0f, 0.0f, -18.0f),
        parameter("ratio", "Ratio", ":1", 1.0f, 20.0f, 4.0f, ParameterScale::logarithmic),
        parameter("knee", "Knee", "dB", 0.0f, 24.0f, 6.0f),
        parameter("attack", "Attack", "ms", 0.1f, 100.0f, 10.0f, ParameterScale::logarithmic),
        parameter("release", "Release", "ms", 10.0f, 2000.0f, 100.0f, ParameterScale::logarithmic),
        parameter("makeup", "Makeup", "dB", 0.0f, 24.0f, 0.0f),
        parameter("mix", "Mix", "%", 0.0f, 100.0f, 100.0f),
    };
}

const EffectDescription& Compressor::getDescription() const noexcept
{
    return description;
}

void Compressor::prepare(const ProcessSetup& setup)
{
    sampleRate = setup.sampleRate;
    attackFor = releaseFor = -1.0f;
    reset();
}

void Compressor::reset() noexcept
{
    smoothed = 0.0;
}

double Compressor::staticCurve(double inputDb, double thresholdDb, double ratio,
                               double kneeDb) noexcept
{
    const double over = inputDb - thresholdDb;
    ratio = std::max(ratio, 1.0);

    if (2.0 * over < -kneeDb)
        return inputDb;
    if (2.0 * std::abs(over) <= kneeDb && kneeDb > 0.0)
    {
        const double bend = over + kneeDb / 2.0;
        return inputDb + (1.0 / ratio - 1.0) * bend * bend / (2.0 * kneeDb);
    }
    return thresholdDb + over / ratio;
}

void Compressor::process(const ProcessBlock& block) noexcept
{
    const auto& values = block.parameters;
    const int numChannels = std::min(block.numChannels, 2);
    float largestReduction = 0.0f;
    float largestLevel = static_cast<float>(silenceDb);

    for (int i = 0; i < block.numSamples; ++i)
    {
        double peak = 0.0;
        for (int channel = 0; channel < numChannels; ++channel)
            peak = std::max(peak, static_cast<double>(std::abs(block.channels[channel][i])));

        // Silence in and nothing left to release: nothing to do.
        if (juce::exactlyEqual(peak, 0.0) && smoothed < 1.0e-6)
        {
            smoothed = 0.0;
            continue;
        }

        const float attackMs = values.at(attack, i);
        const float releaseMs = values.at(release, i);
        if (!juce::exactlyEqual(attackMs, attackFor))
        {
            attackFor = attackMs;
            attackCoefficient = coefficientFor(attackMs, sampleRate);
        }
        if (!juce::exactlyEqual(releaseMs, releaseFor))
        {
            releaseFor = releaseMs;
            releaseCoefficient = coefficientFor(releaseMs, sampleRate);
        }

        const double level = peak > 0.0 ? 20.0 * std::log10(peak) : silenceDb;
        const double wanted =
            level - staticCurve(level, values.at(threshold, i), values.at(ratio, i), values.at(knee, i));

        const double coefficient = wanted > smoothed ? attackCoefficient : releaseCoefficient;
        smoothed = coefficient * smoothed + (1.0 - coefficient) * wanted;

        const double gain = std::pow(10.0, (values.at(makeup, i) - smoothed) / 20.0);
        const double wet = values.at(mix, i) / 100.0;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            const double x = block.channels[channel][i];
            block.channels[channel][i] = static_cast<float>(x * gain * wet + x * (1.0 - wet));
        }

        largestReduction = std::max(largestReduction, static_cast<float>(smoothed));
        largestLevel = std::max(largestLevel, static_cast<float>(level));
    }

    reduction.report(largestReduction);
    if (largestLevel > inputLevel.load(std::memory_order_relaxed))
        inputLevel.store(largestLevel, std::memory_order_relaxed);
}

double Compressor::getTailSeconds(std::span<const float>) const
{
    return 0.0;
}

} // namespace daw::engine::fx
