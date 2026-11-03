#include "daw/engine/AutomationCurve.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{

namespace
{
constexpr double maximumBend = 6.0; // the exponent of the most bent shape
} // namespace

AutomationCurve::AutomationCurve(std::vector<AutomationPoint> newPoints)
    : points(std::move(newPoints))
{
    for (auto& point : points)
    {
        point.value = std::isfinite(point.value) ? std::clamp(point.value, 0.0f, 1.0f) : 0.0f;
        point.curve = std::isfinite(point.curve) ? std::clamp(point.curve, -1.0f, 1.0f) : 0.0f;
    }

    std::stable_sort(points.begin(), points.end(),
                     [](const AutomationPoint& a, const AutomationPoint& b) {
                         return a.position < b.position;
                     });
}

float AutomationCurve::shape(double x, float curve) noexcept
{
    x = std::clamp(x, 0.0, 1.0);
    if (juce::exactlyEqual(curve, 0.0f))
        return static_cast<float>(x);

    const double bend = maximumBend * static_cast<double>(curve);
    return static_cast<float>(std::expm1(bend * x) / std::expm1(bend));
}

int AutomationCurve::segmentAt(juce::int64 position) const noexcept
{
    // The last point at or before position; -1 before the first.
    const auto after = std::upper_bound(
        points.begin(), points.end(), position,
        [](juce::int64 value, const AutomationPoint& point) { return value < point.position; });
    return static_cast<int>(after - points.begin()) - 1;
}

float AutomationCurve::valueIn(int segment, juce::int64 position) const noexcept
{
    if (segment < 0)
        return points.front().value;

    const auto& from = points[static_cast<std::size_t>(segment)];
    if (static_cast<std::size_t>(segment) + 1 >= points.size())
        return from.value;

    const auto& to = points[static_cast<std::size_t>(segment) + 1];
    const double x = static_cast<double>(position - from.position) /
                     static_cast<double>(to.position - from.position);
    return from.value + (to.value - from.value) * shape(x, from.curve);
}

float AutomationCurve::valueAt(juce::int64 position) const noexcept
{
    return points.empty() ? 0.0f : valueIn(segmentAt(position), position);
}

void AutomationCurve::fill(juce::int64 start, int numSamples, float* values) const noexcept
{
    if (points.empty())
    {
        std::fill(values, values + numSamples, 0.0f);
        return;
    }

    int segment = segmentAt(start);
    const auto last = static_cast<int>(points.size()) - 1;

    for (int i = 0; i < numSamples; ++i)
    {
        const juce::int64 position = start + i;
        while (segment < last && points[static_cast<std::size_t>(segment) + 1].position <= position)
            ++segment;
        values[i] = valueIn(segment, position);
    }
}

} // namespace daw::engine
