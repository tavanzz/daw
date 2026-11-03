#pragma once

#include <juce_core/juce_core.h>

#include <vector>

namespace daw::engine
{

/** A point of an automation curve, in samples at the arrangement's rate. */
struct AutomationPoint
{
    juce::int64 position = 0;
    float value = 0.0f; ///< normalised, 0 to 1
    float curve = 0.0f; ///< shape of the segment to the next point: 0 straight, -1 to 1 bent
};

/**
    How a parameter moves over the timeline: values between points, straight or
    bent; before the first point the first value, after the last the last one. Two
    points at the same position make a step (the later one holds from there).

    Every value is a function of the position alone, so automation is exact to the
    sample and never depends on how the timeline is cut into blocks.

    Immutable once built (on the message thread); shared with the audio thread,
    whose calls are real-time safe.
*/
class AutomationCurve
{
public:
    /** points are sorted by position (stably: points at the same position keep their
        order); values and shapes are clamped. */
    explicit AutomationCurve(std::vector<AutomationPoint> points);

    [[nodiscard]] bool isEmpty() const noexcept { return points.empty(); }
    [[nodiscard]] const std::vector<AutomationPoint>& getPoints() const noexcept { return points; }

    /** The value at a position (0 for an empty curve). */
    [[nodiscard]] float valueAt(juce::int64 position) const noexcept;

    /** The values at start, start + 1, ... start + numSamples - 1. */
    void fill(juce::int64 start, int numSamples, float* values) const noexcept;

    /** How far along a segment (0 to 1) the value is at fraction x of its length:
        x itself for curve 0; for curve > 0 slow first, then fast; < 0 the reverse.
        Exactly 0 at 0 and 1 at 1. */
    [[nodiscard]] static float shape(double x, float curve) noexcept;

private:
    [[nodiscard]] int segmentAt(juce::int64 position) const noexcept;
    [[nodiscard]] float valueIn(int segment, juce::int64 position) const noexcept;

    std::vector<AutomationPoint> points;
};

} // namespace daw::engine
