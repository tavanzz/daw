#pragma once

#include <juce_core/juce_core.h>

namespace daw::model
{

/** Positions and lengths on the timeline, in ticks.

    Integers, so that edits, undo and saved files are exact, and independent of any
    sample rate: the project does not have one, the audio device and the export do. */
using Ticks = juce::int64;

/** 282,240,000 ticks per second. Every common sample rate divides it (8, 11.025,
    16, 22.05, 24, 32, 44.1, 48, 88.2, 96, 176.4, 192, 352.8 and 384 kHz), so a
    position given in samples at any of them is exact in ticks and converts back to
    the same sample. */
inline constexpr Ticks ticksPerSecond = 282'240'000;

[[nodiscard]] Ticks secondsToTicks(double seconds) noexcept;
[[nodiscard]] double ticksToSeconds(Ticks ticks) noexcept;

/** The sample at sampleRate nearest to ticks (halfway rounds up). Integer arithmetic,
    hence exact, for rates that divide ticksPerSecond. */
[[nodiscard]] juce::int64 ticksToSamples(Ticks ticks, double sampleRate) noexcept;

/** Ticks of a sample position at sampleRate (exact for rates that divide
    ticksPerSecond, the nearest tick otherwise). */
[[nodiscard]] Ticks samplesToTicks(juce::int64 samples, double sampleRate) noexcept;

} // namespace daw::model
