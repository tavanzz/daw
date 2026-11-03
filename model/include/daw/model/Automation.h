#pragma once

#include "daw/model/Clip.h"
#include "daw/model/TimelineTime.h"

#include <juce_data_structures/juce_data_structures.h>

#include <cstdint>
#include <optional>

namespace daw::model
{

/** What an automation lane moves: a track's or bus's volume or pan (the master has
    volume only), the level of one of a track's sends, or one parameter of one of
    the owner's effects. */
struct AutomationTarget
{
    enum class Kind : std::uint8_t
    {
        volume,
        pan,
        sendLevel,
        effectParameter
    };

    Kind kind = Kind::volume;
    ObjectId object = 0;   ///< the send's or the effect's id
    juce::String parameter; ///< the effect parameter's id

    [[nodiscard]] static AutomationTarget volume() { return {Kind::volume, 0, {}}; }
    [[nodiscard]] static AutomationTarget pan() { return {Kind::pan, 0, {}}; }
    [[nodiscard]] static AutomationTarget sendLevel(ObjectId sendId)
    {
        return {Kind::sendLevel, sendId, {}};
    }
    [[nodiscard]] static AutomationTarget effectParameter(ObjectId effectId, juce::String id)
    {
        return {Kind::effectParameter, effectId, std::move(id)};
    }

    /** As stored in project files: "volume", "pan", "send:12", "effect:9:threshold". */
    [[nodiscard]] juce::String toString() const;
    [[nodiscard]] static std::optional<AutomationTarget> fromString(const juce::String& text);

    bool operator==(const AutomationTarget&) const = default;
};

/** One point of a lane. value is normalised (0 to 1: for volumes and send levels
    the fader's position, for pan 0 hard left to 1 hard right, for effect parameters
    the parameter's own mapping); curve bends the segment to the next point (0
    straight, -1 to 1). */
struct AutomationPointData
{
    Ticks position = 0;
    double value = 0.0;
    double curve = 0.0;

    bool operator==(const AutomationPointData&) const = default;
};

/** Read: lanes move their targets as the timeline plays. Off: they are kept but
    ignored (targets keep their own values). */
enum class AutomationMode : std::uint8_t
{
    off,
    read
};

[[nodiscard]] juce::String toString(AutomationMode mode);
[[nodiscard]] std::optional<AutomationMode> automationModeFromString(const juce::String& text);

/** The points of one target, sorted by position (points at the same position make a
    step, in the order they were added). A handle onto part of a Project's state. */
class AutomationLane
{
public:
    explicit AutomationLane(juce::ValueTree laneState);

    [[nodiscard]] std::optional<AutomationTarget> getTarget() const;

    [[nodiscard]] int getNumPoints() const;
    [[nodiscard]] AutomationPointData getPoint(int index) const;

    /** Adds a point after any others at its position; returns its index. Values and
        curves are clamped (and kept at a float's precision), positions kept at 0 or
        later. */
    int addPoint(AutomationPointData point, juce::UndoManager* undoManager = nullptr);

    /** Changes a point, moving it among the others if its position passes theirs;
        returns its new index. */
    int setPoint(int index, AutomationPointData point, juce::UndoManager* undoManager = nullptr);

    void removePoint(int index, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

/** The automation of a track, a bus or the master: its mode and its lanes, at most
    one per target. */
class Automation
{
public:
    explicit Automation(juce::ValueTree automationState);

    /** Not undoable: a switch, like arming. */
    [[nodiscard]] AutomationMode getMode() const;
    void setMode(AutomationMode mode);

    [[nodiscard]] int getNumLanes() const;
    [[nodiscard]] AutomationLane getLane(int index) const;
    [[nodiscard]] int indexOfLane(const AutomationTarget& target) const; ///< -1 if none

    /** The lane of target, added (empty) if there is none. */
    AutomationLane getOrAddLane(const AutomationTarget& target,
                                juce::UndoManager* undoManager = nullptr);

    void removeLane(int index, juce::UndoManager* undoManager = nullptr);

    /** Removes the lanes of an effect's parameters, or of a send. */
    void removeLanesOfEffect(ObjectId effectId, juce::UndoManager* undoManager = nullptr);
    void removeLaneOfSend(ObjectId sendId, juce::UndoManager* undoManager = nullptr);

    [[nodiscard]] const juce::ValueTree& getState() const noexcept { return state; }

private:
    juce::ValueTree state;
};

} // namespace daw::model
