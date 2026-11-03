#pragma once

#include "daw/ui/desktop/ArrangeState.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <optional>

namespace daw::ui::desktop
{

/**
    Automation in the arrange view: each row's lane drawn over its clips and edited
    with the mouse (TrackLanes hands it its rows while the automation view is on).

    - Click empty space to add a point, at the grid unless Cmd is held; drag a
      point to move it; double-click a point to delete it; Alt+drag a segment to
      bend it. Each of these is one undo step.
    - Right-click: clear the lane, or switch the owner's automation between read
      and off. While off, the lane is drawn dim.
    - Without points, the lane shows the target's own value as a dashed line.
*/
class ThemedLookAndFeel;

class AutomationLanes
{
public:
    explicit AutomationLanes(ArrangeState& sharedState);

    void paintRow(juce::Graphics& g, juce::Rectangle<float> row,
                  const controller::ChainOwner& owner,
                  const ThemedLookAndFeel& look) const;

    void mouseDown(const juce::MouseEvent& event, juce::Rectangle<float> row,
                   const controller::ChainOwner& owner);
    void mouseDrag(const juce::MouseEvent& event);
    void mouseUp();
    void mouseDoubleClick(const juce::MouseEvent& event, juce::Rectangle<float> row,
                          const controller::ChainOwner& owner);

    /** The name of a target, for its owner ("Volume", "Send: Reverb",
        "Equaliser: Bell 1 gain"). */
    [[nodiscard]] static juce::String targetName(const controller::Session& session,
                                                 const controller::ChainOwner& owner,
                                                 const model::AutomationTarget& target);

    /** A target's normalised value in its units ("-6.0 dB", "L 40", "1.20 kHz"). */
    [[nodiscard]] static juce::String valueText(const controller::Session& session,
                                                const model::AutomationTarget& target,
                                                double normalised);

    /** The targets an owner can automate, as a menu; chosen is called with one. */
    static void showTargetMenu(controller::Session& session, const controller::ChainOwner& owner,
                               juce::Component* near,
                               std::function<void(const model::AutomationTarget&)> chosen);

private:
    struct Drag
    {
        controller::ChainOwner owner;
        model::AutomationTarget target;
        juce::Rectangle<float> row;
        int index = -1;
        bool bending = false;
        double startBend = 0.0;
        float startY = 0.0f;
    };

    [[nodiscard]] float yOf(juce::Rectangle<float> row, double value) const;
    [[nodiscard]] double valueAt(juce::Rectangle<float> row, float y) const;
    [[nodiscard]] std::optional<model::AutomationLane> laneOf(const controller::ChainOwner& owner,
                                                              const model::AutomationTarget& target) const;
    [[nodiscard]] double fixedValue(const controller::ChainOwner& owner,
                                    const model::AutomationTarget& target) const;

    ArrangeState& state;
    std::optional<Drag> drag;
};

} // namespace daw::ui::desktop
