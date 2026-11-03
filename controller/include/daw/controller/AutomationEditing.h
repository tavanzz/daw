#pragma once

#include "daw/engine/AutomationCurve.h"
#include "daw/model/Automation.h"

#include <optional>

namespace daw::controller
{

/** The lane as the engine's curve, with positions in ticks: for drawing it, value
    by value, exactly as it plays. */
[[nodiscard]] engine::AutomationCurve automationCurveInTicks(const model::AutomationLane& lane);

/** The lane's value (normalised) at a position, as the engine plays it: the same
    curve, in ticks. Nothing for a lane without points. */
[[nodiscard]] std::optional<double> automationValueAt(const model::AutomationLane& lane,
                                                      model::Ticks position);

/** The point within the tolerances of (position, value), the nearest in time;
    -1 if none. */
[[nodiscard]] int findAutomationPoint(const model::AutomationLane& lane, model::Ticks position,
                                      double value, model::Ticks positionTolerance,
                                      double valueTolerance);

/** The segment a position falls in (the index of its first point); -1 before the
    first point or from the last on. */
[[nodiscard]] int automationSegmentAt(const model::AutomationLane& lane, model::Ticks position);

/** Bending a segment by dragging its middle: the bend after a vertical drag of
    delta (normalised value, up positive) from startBend. Dragging towards the
    straight line's other side bends it that way, whichever way the segment goes. */
[[nodiscard]] double bendForDrag(double startBend, double fromValue, double toValue,
                                 double delta);

} // namespace daw::controller
