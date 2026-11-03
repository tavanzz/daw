#include "ThemeLookAndFeels.h"

#include <algorithm>
#include <cmath>

namespace daw::ui::desktop
{

namespace
{
using appearance::ColourRole;
using appearance::FontRole;

void drawProgressText(juce::Graphics& g, const ThemedLookAndFeel& lookAndFeel,
                      juce::Rectangle<float> area, const juce::String& text)
{
    if (text.isEmpty())
        return;

    const auto& spec = lookAndFeel.getTheme().font(FontRole::numeric);
    g.setColour(lookAndFeel.colour(ColourRole::text));
    g.setFont(lookAndFeel.font(FontRole::numeric, std::min(spec.size, area.getHeight() * 0.7f)));
    g.drawText(text, area, juce::Justification::centred, false);
}

// A gradient fill along the bar through the theme's meter colours, quietest first.
void drawGradientProgress(juce::Graphics& g, const ThemedLookAndFeel& lookAndFeel,
                          juce::ProgressBar& bar, int width, int height, double progress,
                          const juce::String& textToShow)
{
    const auto area = juce::Rectangle<int>(width, height).toFloat();
    const float border = lookAndFeel.getTheme().shape.border;
    const auto outline = lookAndFeel.shapeOf(area.reduced(border / 2.0f), CornerSize::small);

    g.setColour(bar.findColour(juce::ProgressBar::backgroundColourId));
    g.fillPath(outline);

    const auto& stops = lookAndFeel.getTheme().meter.stops;
    const float fraction = progress >= 0.0 && progress <= 1.0 ? static_cast<float>(progress) : 1.0f;
    const auto filled = area.reduced(3.0f).withWidth((area.getWidth() - 6.0f) * fraction);

    juce::ColourGradient gradient(toJuce(stops.back().colour), area.getX(), 0.0f,
                                  toJuce(stops.front().colour), area.getRight(), 0.0f, false);
    for (std::size_t index = 1; index + 1 < stops.size(); ++index)
        gradient.addColour(1.0 - static_cast<double>(index) / static_cast<double>(stops.size() - 1),
                           toJuce(stops[index].colour));

    g.setGradientFill(gradient);
    g.fillPath(lookAndFeel.shapeOf(filled, CornerSize::small));

    g.setColour(lookAndFeel.colour(ColourRole::outline));
    g.strokePath(outline, juce::PathStrokeType(border));
    drawProgressText(g, lookAndFeel, area, textToShow);
}
} // namespace

void NeonGridLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width,
                                          int height, double progress,
                                          const juce::String& textToShow)
{
    drawGradientProgress(g, *this, bar, width, height, progress, textToShow);
}

//==============================================================================
juce::String AcidTerminalLookAndFeel::labelText(FontRole role, const juce::String& text) const
{
    const auto shown = ThemedLookAndFeel::labelText(role, text);

    if (role == FontRole::heading && shown.isNotEmpty())
        return "[ " + shown.toUpperCase() + " ]";

    return shown;
}

void AcidTerminalLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width,
                                              int height, double progress,
                                              const juce::String& textToShow)
{
    const auto area = juce::Rectangle<int>(width, height).toFloat();
    g.setColour(bar.findColour(juce::ProgressBar::backgroundColourId));
    g.fillRect(area);

    // Blocks like a terminal's [#####.....]: lit up to the progress.
    const auto inner = area.reduced(4.0f);
    const float blockWidth = 6.0f;
    const float gap = 2.0f;
    const int blocks = std::max(1, static_cast<int>((inner.getWidth() + gap) / (blockWidth + gap)));
    const int lit = progress >= 0.0 && progress <= 1.0
                        ? static_cast<int>(std::round(progress * static_cast<double>(blocks)))
                        : blocks;

    for (int index = 0; index < blocks; ++index)
    {
        g.setColour(index < lit ? colour(ColourRole::accent) : colour(ColourRole::raised));
        g.fillRect(inner.getX() + static_cast<float>(index) * (blockWidth + gap), inner.getY(),
                   blockWidth, inner.getHeight());
    }

    g.setColour(colour(ColourRole::outline));
    g.drawRect(area, theme.shape.border);

    if (textToShow.isNotEmpty())
    {
        // Text over the blocks needs a backing to stay readable on lit and unlit ones.
        const auto textFont = font(FontRole::numeric, std::min(theme.font(FontRole::numeric).size,
                                                               area.getHeight() * 0.7f));
        const float textWidth =
            juce::GlyphArrangement::getStringWidth(textFont, textToShow) + 12.0f;
        g.setColour(colour(ColourRole::inset));
        g.fillRect(area.withSizeKeepingCentre(textWidth, textFont.getHeight() + 2.0f));
        drawProgressText(g, *this, area, textToShow);
    }
}

//==============================================================================
void SynthChromeLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                                  const juce::Colour& backgroundColour,
                                                  bool isMouseOverButton, bool isButtonDown)
{
    ThemedLookAndFeel::drawButtonBackground(g, button, backgroundColour, isMouseOverButton,
                                            isButtonDown);

    // A chrome sheen: light from above over the upper half.
    const auto area = button.getLocalBounds().toFloat().reduced(theme.shape.border);
    juce::ColourGradient sheen(colour(ColourRole::text).withAlpha(0.16f), 0.0f, area.getY(),
                               juce::Colours::transparentWhite, 0.0f, area.getCentreY(), false);
    g.setGradientFill(sheen);
    g.fillPath(shapeOf(area.withHeight(area.getHeight() / 2.0f), CornerSize::small));
}

void SynthChromeLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width,
                                             int height, double progress,
                                             const juce::String& textToShow)
{
    drawGradientProgress(g, *this, bar, width, height, progress, textToShow);
}

} // namespace daw::ui::desktop
