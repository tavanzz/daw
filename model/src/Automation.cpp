#include "daw/model/Automation.h"

#include "Identifiers.h"

#include <algorithm>
#include <cmath>

namespace daw::model
{

juce::String AutomationTarget::toString() const
{
    switch (kind)
    {
    case Kind::volume:
        return "volume";
    case Kind::pan:
        return "pan";
    case Kind::sendLevel:
        return "send:" + juce::String(object);
    case Kind::effectParameter:
        return "effect:" + juce::String(object) + ":" + parameter;
    }
    return "volume";
}

std::optional<AutomationTarget> AutomationTarget::fromString(const juce::String& text)
{
    if (text == "volume")
        return volume();
    if (text == "pan")
        return pan();

    const auto parts = juce::StringArray::fromTokens(text, ":", "");
    const auto isId = [](const juce::String& part) {
        return part.isNotEmpty() && part.containsOnly("0123456789") && part.getLargeIntValue() > 0;
    };

    if (parts.size() == 2 && parts[0] == "send" && isId(parts[1]))
        return sendLevel(parts[1].getLargeIntValue());

    // A parameter id may itself hold colons: everything after the effect id.
    if (parts.size() >= 3 && parts[0] == "effect" && isId(parts[1]))
    {
        const auto parameter = text.fromFirstOccurrenceOf(parts[1] + ":", false, false);
        if (parameter.isNotEmpty())
            return effectParameter(parts[1].getLargeIntValue(), parameter);
    }

    return std::nullopt;
}

juce::String toString(AutomationMode mode)
{
    return mode == AutomationMode::off ? "off" : "read";
}

std::optional<AutomationMode> automationModeFromString(const juce::String& text)
{
    if (text == "off")
        return AutomationMode::off;
    if (text == "read")
        return AutomationMode::read;
    return std::nullopt;
}

namespace
{

// Values and curves at the precision of a float, as the engine uses them, so that
// project files hold them exactly.
AutomationPointData clamped(AutomationPointData point)
{
    const auto asFloat = [](double value) {
        return static_cast<double>(static_cast<float>(value));
    };
    point.position = std::max<Ticks>(0, point.position);
    point.value = std::isfinite(point.value) ? asFloat(std::clamp(point.value, 0.0, 1.0)) : 0.0;
    point.curve = std::isfinite(point.curve) ? asFloat(std::clamp(point.curve, -1.0, 1.0)) : 0.0;
    return point;
}

void write(juce::ValueTree& tree, const AutomationPointData& point, juce::UndoManager* undoManager)
{
    tree.setProperty(ids::position, point.position, undoManager);
    tree.setProperty(ids::value, point.value, undoManager);
    tree.setProperty(ids::curve, point.curve, undoManager);
}

} // namespace

AutomationLane::AutomationLane(juce::ValueTree laneState)
    : state(std::move(laneState))
{
    jassert(state.hasType(ids::lane));
}

std::optional<AutomationTarget> AutomationLane::getTarget() const
{
    return AutomationTarget::fromString(state[ids::target].toString());
}

int AutomationLane::getNumPoints() const
{
    return state.getNumChildren();
}

AutomationPointData AutomationLane::getPoint(int index) const
{
    const auto point = state.getChild(index);
    return {static_cast<Ticks>(point[ids::position]), static_cast<double>(point[ids::value]),
            static_cast<double>(point[ids::curve])};
}

int AutomationLane::addPoint(AutomationPointData point, juce::UndoManager* undoManager)
{
    point = clamped(point);
    int index = 0;
    while (index < getNumPoints() && getPoint(index).position <= point.position)
        ++index;

    juce::ValueTree pointState(ids::point);
    write(pointState, point, nullptr);
    state.addChild(pointState, index, undoManager);
    return index;
}

int AutomationLane::setPoint(int index, AutomationPointData point, juce::UndoManager* undoManager)
{
    if (index < 0 || index >= getNumPoints())
        return index;

    point = clamped(point);

    // Where it belongs among the others: after those before it, before those after.
    int target = index;
    while (target > 0 && getPoint(target - 1).position > point.position)
        --target;
    while (target < getNumPoints() - 1 && getPoint(target + 1).position < point.position)
        ++target;

    if (target != index)
        state.moveChild(index, target, undoManager);

    auto pointState = state.getChild(target);
    write(pointState, point, undoManager);
    return target;
}

void AutomationLane::removePoint(int index, juce::UndoManager* undoManager)
{
    if (index >= 0 && index < getNumPoints())
        state.removeChild(index, undoManager);
}

Automation::Automation(juce::ValueTree automationState)
    : state(std::move(automationState))
{
    jassert(state.hasType(ids::automation));
}

AutomationMode Automation::getMode() const
{
    return automationModeFromString(state[ids::mode].toString()).value_or(AutomationMode::read);
}

void Automation::setMode(AutomationMode mode)
{
    state.setProperty(ids::mode, toString(mode), nullptr);
}

int Automation::getNumLanes() const
{
    return state.getNumChildren();
}

AutomationLane Automation::getLane(int index) const
{
    jassert(index >= 0 && index < getNumLanes());
    return AutomationLane(state.getChild(index));
}

int Automation::indexOfLane(const AutomationTarget& target) const
{
    const auto text = target.toString();
    for (int index = 0; index < getNumLanes(); ++index)
        if (state.getChild(index)[ids::target].toString() == text)
            return index;
    return -1;
}

AutomationLane Automation::getOrAddLane(const AutomationTarget& target,
                                        juce::UndoManager* undoManager)
{
    if (const int index = indexOfLane(target); index >= 0)
        return getLane(index);

    juce::ValueTree lane(ids::lane);
    lane.setProperty(ids::target, target.toString(), nullptr);
    state.appendChild(lane, undoManager);
    return AutomationLane(lane);
}

void Automation::removeLane(int index, juce::UndoManager* undoManager)
{
    if (index >= 0 && index < getNumLanes())
        state.removeChild(index, undoManager);
}

void Automation::removeLanesOfEffect(ObjectId effectId, juce::UndoManager* undoManager)
{
    for (int index = getNumLanes() - 1; index >= 0; --index)
    {
        const auto target = getLane(index).getTarget();
        if (target && target->kind == AutomationTarget::Kind::effectParameter &&
            target->object == effectId)
            removeLane(index, undoManager);
    }
}

void Automation::removeLaneOfSend(ObjectId sendId, juce::UndoManager* undoManager)
{
    removeLane(indexOfLane(AutomationTarget::sendLevel(sendId)), undoManager);
}

} // namespace daw::model
