#include "daw/ui/desktop/LevelMeter.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

namespace daw::ui::desktop
{

namespace
{
constexpr int clipIndicatorHeight = 8;
constexpr int scaleWidth = 22;
constexpr float scaleMarksDb[] = {0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f};
} // namespace

LevelMeter::LevelMeter()
{
    setTooltip("Output level (dBFS). Click to clear the clip indicators.");
}

void LevelMeter::update(float leftPeak, float rightPeak, double elapsedSeconds)
{
    channels[0].update(leftPeak, elapsedSeconds);
    channels[1].update(rightPeak, elapsedSeconds);
    repaint();
}

void LevelMeter::setScaleVisible(bool shouldShowScale)
{
    scaleVisible = shouldShowScale;
    repaint();
}

void LevelMeter::paint(juce::Graphics& g)
{
    const auto& lookAndFeel = ThemedLookAndFeel::get(*this);
    auto area = getLocalBounds().toFloat();
    auto scaleArea = area.removeFromRight(scaleVisible ? static_cast<float>(scaleWidth) : 0.0f);
    const auto clipArea = area.removeFromTop(static_cast<float>(clipIndicatorHeight));
    area.removeFromTop(2.0f);

    const float barWidth = (area.getWidth() - 2.0f) / 2.0f;

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto& meter = channels[static_cast<std::size_t>(channel)];
        const float x = area.getX() + static_cast<float>(channel) * (barWidth + 2.0f);

        lookAndFeel.drawMeterBar(g, {x, area.getY(), barWidth, area.getHeight()},
                                 meter.getLevelDb(), meter.getPeakHoldDb());
        lookAndFeel.drawClipIndicator(g, {x, clipArea.getY(), barWidth, clipArea.getHeight()},
                                      meter.isClipping());
    }

    if (scaleVisible)
        lookAndFeel.drawMeterScale(g, scaleArea.withTrimmedTop(clipArea.getHeight() + 2.0f),
                                   scaleMarksDb);
}

void LevelMeter::mouseDown(const juce::MouseEvent&)
{
    for (auto& meter : channels)
        meter.resetClip();
    repaint();
}

} // namespace daw::ui::desktop
