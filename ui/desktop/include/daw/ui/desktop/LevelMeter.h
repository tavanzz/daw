#pragma once

#include "daw/engine/dsp/MeterBallistics.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace daw::ui::desktop
{

/** Stereo peak meter: level bars, peak-hold markers and clip indicators (click the
    meter to clear them). The ballistics come from engine::dsp::MeterBallistics. */
class LevelMeter final : public juce::Component, public juce::SettableTooltipClient
{
public:
    LevelMeter();

    /** Peaks since the previous update (linear gain) and the time that has passed. */
    void update(float leftPeak, float rightPeak, double elapsedSeconds);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    std::array<engine::dsp::MeterBallistics, 2> channels;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

} // namespace daw::ui::desktop
