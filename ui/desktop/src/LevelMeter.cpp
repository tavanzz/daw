#include "daw/ui/desktop/LevelMeter.h"

namespace daw::ui::desktop
{

namespace
{
using engine::dsp::MeterBallistics;

const juce::Colour trackColour{0xff15181c};
const juce::Colour safeColour{0xff43a047};
const juce::Colour warningColour{0xfffdd835};
const juce::Colour dangerColour{0xffe53935};

constexpr int clipIndicatorHeight = 8;
constexpr int scaleWidth = 22;
constexpr float scaleMarksDb[] = {0.0f, -6.0f, -12.0f, -24.0f, -36.0f, -48.0f};

float yForDecibels(float decibels, juce::Rectangle<float> area)
{
    return area.getBottom() - MeterBallistics::toProportion(decibels) * area.getHeight();
}
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

void LevelMeter::paint(juce::Graphics& g)
{
    auto area = getLocalBounds().toFloat();
    auto scaleArea = area.removeFromRight(static_cast<float>(scaleWidth));
    const auto clipArea = area.removeFromTop(static_cast<float>(clipIndicatorHeight));
    area.removeFromTop(2.0f);

    const float barWidth = (area.getWidth() - 2.0f) / 2.0f;

    // Red at 0 dB, yellow at -3 dB, green from -12 dB down. Gradient positions run
    // from 0 at the top of the scale to 1 at its floor.
    juce::ColourGradient colours(dangerColour, 0.0f, yForDecibels(0.0f, area), safeColour, 0.0f,
                                 yForDecibels(MeterBallistics::floorDb, area), false);
    colours.addColour(1.0 - MeterBallistics::toProportion(-3.0f), warningColour);
    colours.addColour(1.0 - MeterBallistics::toProportion(-12.0f), safeColour);

    for (int channel = 0; channel < 2; ++channel)
    {
        const auto& meter = channels[static_cast<std::size_t>(channel)];
        const float x = area.getX() + static_cast<float>(channel) * (barWidth + 2.0f);
        const juce::Rectangle<float> bar(x, area.getY(), barWidth, area.getHeight());

        g.setColour(trackColour);
        g.fillRect(bar);

        const float levelTop = yForDecibels(meter.getLevelDb(), bar);
        g.setGradientFill(colours);
        g.fillRect(bar.withTop(levelTop));

        if (meter.getPeakHoldDb() > MeterBallistics::floorDb)
        {
            g.setColour(juce::Colours::white.withAlpha(0.8f));
            g.fillRect(bar.withTop(yForDecibels(meter.getPeakHoldDb(), bar)).withHeight(2.0f));
        }

        g.setColour(meter.isClipping() ? dangerColour : trackColour);
        g.fillRect(juce::Rectangle<float>(x, clipArea.getY(), barWidth, clipArea.getHeight()));
    }

    g.setColour(juce::Colours::grey);
    g.setFont(juce::FontOptions(10.0f));
    for (const float decibels : scaleMarksDb)
    {
        const float y = yForDecibels(decibels, area);
        g.drawText(juce::String(static_cast<int>(decibels)),
                   juce::Rectangle<float>(scaleArea.getX() + 2.0f, y - 6.0f, scaleArea.getWidth() - 2.0f, 12.0f),
                   juce::Justification::centredLeft);
    }
}

void LevelMeter::mouseDown(const juce::MouseEvent&)
{
    for (auto& meter : channels)
        meter.resetClip();
    repaint();
}

} // namespace daw::ui::desktop
