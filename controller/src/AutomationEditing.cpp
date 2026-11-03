#include "daw/controller/AutomationEditing.h"

#include "daw/engine/AutomationCurve.h"

#include <algorithm>
#include <cmath>
#include <vector>

namespace daw::controller
{

engine::AutomationCurve automationCurveInTicks(const model::AutomationLane& lane)
{
    // Ticks instead of samples: the same shape between the same points.
    std::vector<engine::AutomationPoint> points;
    for (int i = 0; i < lane.getNumPoints(); ++i)
    {
        const auto point = lane.getPoint(i);
        points.push_back({point.position, static_cast<float>(point.value),
                          static_cast<float>(point.curve)});
    }
    return engine::AutomationCurve(std::move(points));
}

std::optional<double> automationValueAt(const model::AutomationLane& lane, model::Ticks position)
{
    if (lane.getNumPoints() == 0)
        return std::nullopt;
    return automationCurveInTicks(lane).valueAt(position);
}

int findAutomationPoint(const model::AutomationLane& lane, model::Ticks position, double value,
                        model::Ticks positionTolerance, double valueTolerance)
{
    int best = -1;
    model::Ticks bestDistance = 0;
    for (int i = 0; i < lane.getNumPoints(); ++i)
    {
        const auto point = lane.getPoint(i);
        const auto distance = std::abs(point.position - position);
        if (distance <= positionTolerance && std::abs(point.value - value) <= valueTolerance &&
            (best < 0 || distance < bestDistance))
        {
            best = i;
            bestDistance = distance;
        }
    }
    return best;
}

int automationSegmentAt(const model::AutomationLane& lane, model::Ticks position)
{
    for (int i = 0; i + 1 < lane.getNumPoints(); ++i)
        if (lane.getPoint(i).position <= position && position < lane.getPoint(i + 1).position)
            return i;
    return -1;
}

double bendForDrag(double startBend, double fromValue, double toValue, double delta)
{
    // A positive bend sags (slow first, then fast). For a rising segment, dragging
    // down sags it; for a falling one, dragging up does: the sign follows the slope.
    const double direction = toValue >= fromValue ? -1.0 : 1.0;
    return std::clamp(startBend + direction * 2.0 * delta, -1.0, 1.0);
}

} // namespace daw::controller
