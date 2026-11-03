#pragma once

#include "daw/engine/dsp/MeterBallistics.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>
#include <optional>

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

    /** The dB scale beside the bars (shown by default). */
    void setScaleVisible(bool shouldShowScale);

    void paint(juce::Graphics& g) override;
    void mouseDown(const juce::MouseEvent& event) override;

private:
    std::array<engine::dsp::MeterBallistics, 2> channels;
    std::array<float, 2> afterglowDb{engine::dsp::MeterBallistics::floorDb,
                                     engine::dsp::MeterBallistics::floorDb};

    // What was last painted: a meter that would look the same is not repainted.
    struct Shown
    {
        std::array<float, 2> levelDb{};
        std::array<float, 2> peakHoldDb{};
        std::array<float, 2> afterglowDb{};
        std::array<bool, 2> clipping{};
    };
    std::optional<Shown> shown;
    bool scaleVisible = true;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(LevelMeter)
};

} // namespace daw::ui::desktop
