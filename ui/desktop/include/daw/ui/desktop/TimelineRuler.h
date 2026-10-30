#pragma once

#include "daw/ui/desktop/ArrangeState.h"

#include <juce_gui_basics/juce_gui_basics.h>

namespace daw::ui::desktop
{

/**
    The ruler above the tracks: bars and beats on top, minutes and seconds below,
    the loop region and the playhead.

    Click to move the playhead; drag to set the loop region (which turns loop on).
    Both snap to the grid unless Cmd is held.
*/
class TimelineRuler final : public juce::Component, public juce::SettableTooltipClient
{
public:
    static constexpr int height = 44;

    explicit TimelineRuler(ArrangeState& sharedState);

    void setPlayhead(model::Ticks position);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;
    void mouseDrag(const juce::MouseEvent& event) override;
    void mouseUp(const juce::MouseEvent& event) override;
    void mouseWheelMove(const juce::MouseEvent& event,
                        const juce::MouseWheelDetails& wheel) override;
    void mouseMagnify(const juce::MouseEvent& event, float scaleFactor) override;

private:
    ArrangeState& state;
    model::Ticks playhead = 0;
    model::Ticks dragStart = 0;
    bool settingLoop = false;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(TimelineRuler)
};

} // namespace daw::ui::desktop
