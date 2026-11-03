#include "daw/ui/desktop/LevelMeter.h"

#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <algorithm>
#include <cmath>

namespace daw::ui::desktop
{

namespace
{
constexpr int clipIndicatorHeight = 8;
constexpr int scaleWidth = 22;
constexpr float scaleMarksDb[] = {0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f};
// LEDs fade out more slowly than the level falls.
constexpr float afterglowFallDbPerSecond = 12.0f;
} // namespace

LevelMeter::LevelMeter()
{
    setTooltip("Output level (dBFS). Click to clear the clip indicators.");
}

void LevelMeter::update(float leftPeak, float rightPeak, double elapsedSeconds)
{
    channels[0].update(leftPeak, elapsedSeconds);
    channels[1].update(rightPeak, elapsedSeconds);

    const auto fall = afterglowFallDbPerSecond * static_cast<float>(elapsedSeconds);
    for (std::size_t channel = 0; channel < channels.size(); ++channel)
        afterglowDb[channel] =
            std::max(channels[channel].getLevelDb(), afterglowDb[channel] - fall);

    // Silent or steady meters (most of them, most of the time) are not repainted: a
    // change smaller than a twentieth of a dB moves nothing on screen.
    Shown now;
    for (std::size_t channel = 0; channel < channels.size(); ++channel)
    {
        now.levelDb[channel] = channels[channel].getLevelDb();
        now.peakHoldDb[channel] = channels[channel].getPeakHoldDb();
        now.afterglowDb[channel] = afterglowDb[channel];
        now.clipping[channel] = channels[channel].isClipping();
    }

    const auto unchanged = [](const std::array<float, 2>& a, const std::array<float, 2>& b)
    { return std::abs(a[0] - b[0]) < 0.05f && std::abs(a[1] - b[1]) < 0.05f; };

    if (shown && unchanged(shown->levelDb, now.levelDb) &&
        unchanged(shown->peakHoldDb, now.peakHoldDb) &&
        unchanged(shown->afterglowDb, now.afterglowDb) && shown->clipping == now.clipping)
        return;

    shown = now;
    repaint();
}

void LevelMeter::setScaleVisible(bool shouldShowScale)
{
    scaleVisible = shouldShowScale;
    repaint();
}

void LevelMeter::paint(juce::Graphics& g)
{
    const auto& look = ThemedLookAndFeel::get(*this);
    auto area = getLocalBounds().toFloat();
    auto scaleArea = area.removeFromRight(scaleVisible ? static_cast<float>(scaleWidth) : 0.0f);
    const auto clipArea = area.removeFromTop(static_cast<float>(clipIndicatorHeight));
    area.removeFromTop(2.0f);

    const float barWidth = (area.getWidth() - 2.0f) / 2.0f;
    const bool animated = look.isAnimating(appearance::AnimationEffect::animatedMeters);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto& meter = channels[static_cast<std::size_t>(channel)];
        const float x = area.getX() + static_cast<float>(channel) * (barWidth + 2.0f);

        look.drawMeterBar(g, {x, area.getY(), barWidth, area.getHeight()},
                                 meter.getLevelDb(), meter.getPeakHoldDb(),
                                 {animated, afterglowDb[static_cast<std::size_t>(channel)]});
        look.drawClipIndicator(g, {x, clipArea.getY(), barWidth, clipArea.getHeight()},
                                      meter.isClipping());
    }

    if (scaleVisible)
        look.drawMeterScale(g, scaleArea.withTrimmedTop(clipArea.getHeight() + 2.0f),
                                   scaleMarksDb);
}

void LevelMeter::mouseDown(const juce::MouseEvent&)
{
    for (auto& meter : channels)
        meter.resetClip();
    shown.reset();
    repaint();
}

} // namespace daw::ui::desktop
