#include "daw/model/TimelineTime.h"

#include <cmath>
#include <optional>

namespace daw::model
{

namespace
{

// Ticks per sample for rates that divide ticksPerSecond; nothing for other rates.
std::optional<juce::int64> exactTicksPerSample(double sampleRate) noexcept
{
    if (!(sampleRate >= 1.0) || sampleRate > static_cast<double>(ticksPerSecond))
        return std::nullopt;

    const auto rate = static_cast<juce::int64>(sampleRate);
    if (!juce::exactlyEqual(static_cast<double>(rate), sampleRate) || ticksPerSecond % rate != 0)
        return std::nullopt;

    return ticksPerSecond / rate;
}

juce::int64 floorDivide(juce::int64 numerator, juce::int64 denominator) noexcept
{
    const auto quotient = numerator / denominator;
    const bool inexact = quotient * denominator != numerator;
    return inexact && ((numerator < 0) != (denominator < 0)) ? quotient - 1 : quotient;
}

} // namespace

Ticks secondsToTicks(double seconds) noexcept
{
    return static_cast<Ticks>(std::llround(seconds * static_cast<double>(ticksPerSecond)));
}

double ticksToSeconds(Ticks ticks) noexcept
{
    return static_cast<double>(ticks) / static_cast<double>(ticksPerSecond);
}

juce::int64 ticksToSamples(Ticks ticks, double sampleRate) noexcept
{
    jassert(sampleRate > 0.0);

    if (const auto ticksPerSample = exactTicksPerSample(sampleRate))
        return floorDivide(ticks + *ticksPerSample / 2, *ticksPerSample);

    return static_cast<juce::int64>(std::floor(
        static_cast<double>(ticks) * sampleRate / static_cast<double>(ticksPerSecond) + 0.5));
}

Ticks samplesToTicks(juce::int64 samples, double sampleRate) noexcept
{
    jassert(sampleRate > 0.0);

    if (const auto ticksPerSample = exactTicksPerSample(sampleRate))
        return samples * *ticksPerSample;

    const double seconds = static_cast<double>(samples) / sampleRate;
    return static_cast<Ticks>(std::llround(seconds * static_cast<double>(ticksPerSecond)));
}

} // namespace daw::model
