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

// The logo is larger than titles: it fills most of its area.
float logoHeight(juce::Rectangle<float> area)
{
    return std::min(26.0f, area.getHeight() * 0.62f);
}

// The logo's letters as a path, centred in the area.
juce::Path logoPath(const ThemedLookAndFeel& lookAndFeel, juce::Rectangle<float> area,
                    const juce::String& text)
{
    const auto& spec = lookAndFeel.getTheme().font(FontRole::title);
    juce::GlyphArrangement glyphs;
    glyphs.addFittedText(lookAndFeel.font(FontRole::title, logoHeight(area)),
                         ThemeFonts::textFor(spec, text), area.getX(), area.getY(), area.getWidth(),
                         area.getHeight(), juce::Justification::centred, 1);
    juce::Path path;
    glyphs.createPath(path);
    return path;
}
} // namespace

void NeonGridLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width,
                                          int height, double progress,
                                          const juce::String& textToShow)
{
    drawGradientProgress(g, *this, bar, width, height, progress, textToShow);
}

void NeonGridLookAndFeel::drawLogo(juce::Graphics& g, juce::Rectangle<float> area,
                                   const juce::String& text, double seconds, bool glitching) const
{
    // A neon tube: the letters glowing in the accent. Now and then it flickers, as a
    // tube about to fail does: two quick dips in a cycle of a few seconds.
    float brightness = 1.0f;
    if (glitching)
    {
        const double t = std::fmod(seconds, 4.7);
        if ((t >= 4.0 && t < 4.06) || (t >= 4.12 && t < 4.16))
            brightness = 0.25f;
        else if (t >= 4.30 && t < 4.33)
            brightness = 0.55f;
    }

    const auto letters = logoPath(*this, area, text);
    drawGlow(g, letters, colour(ColourRole::accent), brightness);
    g.setColour(colour(ColourRole::accent).withMultipliedAlpha(0.35f + 0.65f * brightness));
    g.fillPath(letters);
}

void NeonGridLookAndFeel::drawTimelineBackground(juce::Graphics& g, juce::Rectangle<float> area,
                                                 double seconds) const
{
    // Scanlines: a dark line every few pixels, drifting down, from a prerendered tile;
    // and a faint bright band sweeping down the screen.
    constexpr int period = 4;
    const auto tile =
        getImages().get("scanlines", {8, period},
                        [this](juce::Graphics& tileGraphics, juce::Rectangle<int>)
                        {
                            tileGraphics.setColour(colour(ColourRole::window).withAlpha(0.35f));
                            tileGraphics.fillRect(0, 0, 8, 1);
                        });
    const auto drift = static_cast<int>(std::fmod(seconds * 8.0, static_cast<double>(period)));
    g.setTiledImageFill(tile, 0, drift, 1.0f);
    g.fillRect(area);

    const float bandHeight = 160.0f;
    const float travel = area.getHeight() + bandHeight;
    const float y = area.getY() - bandHeight +
                    static_cast<float>(std::fmod(seconds * 45.0, static_cast<double>(travel)));
    const auto accent = colour(ColourRole::accent);
    juce::ColourGradient band(accent.withAlpha(0.0f), 0.0f, y, accent.withAlpha(0.0f), 0.0f,
                              y + bandHeight, false);
    band.addColour(0.5, accent.withAlpha(0.06f));
    g.setGradientFill(band);
    g.fillRect(area.withY(y).withHeight(bandHeight));
}

bool NeonGridLookAndFeel::playheadAnimates(bool) const
{
    return true;
}

void NeonGridLookAndFeel::drawPlayhead(juce::Graphics& g, float x, float top, float bottom,
                                       const PlayheadLook& look) const
{
    // The glow breathes, the line stays.
    const auto line = juce::Rectangle<float>(x - 0.75f, top, 1.5f, bottom - top);
    juce::Path shape;
    shape.addRectangle(line);
    drawGlow(g, shape, colour(ColourRole::playhead),
             look.animated ? 0.45f + 0.55f * pulseAt(look.seconds * 0.8) : 1.0f);

    g.setColour(colour(ColourRole::playhead));
    g.fillRect(line);
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

void AcidTerminalLookAndFeel::drawLogo(juce::Graphics& g, juce::Rectangle<float> area,
                                       const juce::String& text, double seconds,
                                       bool glitching) const
{
    // A block in the accent with the name cut out of it, like a terminal's banner.
    const auto letters = logoPath(*this, area, text);
    const auto block = letters.getBounds().expanded(8.0f, 4.0f);
    const auto ink = textColourOn(colour(ColourRole::accent));

    // For a moment every few seconds the banner glitches: slices of it jump sideways
    // and a ghost of the name in the second accent slips out.
    const double t = std::fmod(seconds, 3.3);
    if (!glitching || t >= 0.16)
    {
        g.setColour(colour(ColourRole::accent));
        g.fillRect(block);
        g.setColour(ink);
        g.fillPath(letters);
        return;
    }

    const int step = static_cast<int>(t * 50.0);
    const float slice = block.getHeight() / 3.0f;
    for (int index = 0; index < 3; ++index)
    {
        const float shift = static_cast<float>(((step + index * 2) % 5) - 2) * 2.0f;
        const auto band =
            block.withY(block.getY() + slice * static_cast<float>(index)).withHeight(slice);

        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(band.expanded(8.0f, 0.0f).toNearestInt());
        g.addTransform(juce::AffineTransform::translation(shift, 0.0f));
        g.setColour(colour(ColourRole::accent));
        g.fillRect(block);
        g.setColour(colour(ColourRole::accentAlt).withAlpha(0.8f));
        g.fillPath(letters, juce::AffineTransform::translation(2.0f, 0.0f));
        g.setColour(ink);
        g.fillPath(letters);
    }
}

void AcidTerminalLookAndFeel::drawHazardBand(juce::Graphics& g, juce::Rectangle<float> area,
                                             double seconds, bool recording) const
{
    // Diagonal stripes, as on hazard tape: acid on black, red and faster while
    // recording.
    constexpr float period = 16.0f;
    g.setColour(colour(ColourRole::window));
    g.fillRect(area);

    const double speed = recording ? 60.0 : 24.0;
    const auto offset = static_cast<float>(std::fmod(seconds * speed, static_cast<double>(period)));
    const float height = area.getHeight();

    juce::Path stripes;
    for (float x = area.getX() - period - height + offset; x < area.getRight(); x += period)
    {
        stripes.startNewSubPath(x, area.getBottom());
        stripes.lineTo(x + height, area.getY());
        stripes.lineTo(x + height + period / 2.0f, area.getY());
        stripes.lineTo(x + period / 2.0f, area.getBottom());
        stripes.closeSubPath();
    }

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(area.toNearestInt());
    g.setColour(colour(recording ? ColourRole::record : ColourRole::accent));
    g.fillPath(stripes);
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

void SynthChromeLookAndFeel::drawLogo(juce::Graphics& g, juce::Rectangle<float> area,
                                      const juce::String& text, double seconds,
                                      bool glitching) const
{
    // Chrome letters: light at the top, a dark band at the horizon, light again below,
    // over a line in the accent.
    const auto letters = logoPath(*this, area, text);
    const auto bounds = letters.getBounds();
    const auto light = colour(ColourRole::text);
    const auto dark = colour(ColourRole::textSecondary);

    juce::ColourGradient chrome(light, 0.0f, bounds.getY(), light, 0.0f, bounds.getBottom(), false);
    chrome.addColour(0.48, dark);
    chrome.addColour(0.52, colour(ColourRole::accentAlt));
    g.setGradientFill(chrome);
    g.fillPath(letters);

    g.setColour(colour(ColourRole::accent));
    g.fillRect(bounds.withY(bounds.getBottom() + 3.0f).withHeight(2.0f));

    // Every few seconds a reflection sweeps across the chrome.
    const double t = std::fmod(seconds, 5.0);
    if (!glitching || t >= 1.2)
        return;

    const float x =
        bounds.getX() - 24.0f + static_cast<float>(t / 1.2) * (bounds.getWidth() + 48.0f);
    juce::Path streak;
    streak.addQuadrilateral(x, bounds.getY(), x + 10.0f, bounds.getY(), x + 4.0f,
                            bounds.getBottom(), x - 6.0f, bounds.getBottom());

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(letters);
    g.setColour(colour(ColourRole::text).withAlpha(0.85f));
    g.fillPath(streak);
}

void SynthChromeLookAndFeel::drawTimelineBackground(juce::Graphics& g, juce::Rectangle<float> area,
                                                    double seconds) const
{
    // A floor grid running to a horizon at the top: rays from the vanishing point and
    // cross lines coming closer, brighter as they near.
    const auto line = colour(ColourRole::accentAlt);
    const float horizon = area.getY();
    const float depth = area.getHeight();
    const float centre = area.getCentreX();

    g.setColour(line.withAlpha(0.16f));
    const float spacing = std::max(120.0f, area.getWidth() / 5.0f);
    for (float bottomX = centre - 3.0f * area.getWidth();
         bottomX <= centre + 3.0f * area.getWidth(); bottomX += spacing)
        g.drawLine(centre, horizon, bottomX, area.getBottom(), 1.0f);

    constexpr int rows = 12;
    const auto phase = static_cast<float>(std::fmod(seconds * 0.25, 1.0));
    for (int index = 0; index < rows; ++index)
    {
        const float z = (static_cast<float>(index) + phase) / static_cast<float>(rows);
        const float y = horizon + depth * z * z;
        g.setColour(line.withAlpha(0.05f + 0.25f * z));
        g.drawHorizontalLine(juce::roundToInt(y), area.getX(), area.getRight());
    }
}

void SynthChromeLookAndFeel::drawPlayhead(juce::Graphics& g, float x, float top, float bottom,
                                          const PlayheadLook& look) const
{
    // While playing, a fading trail behind the line.
    if (look.animated && look.playing)
    {
        const auto trail = colour(ColourRole::playhead);
        const auto width = static_cast<float>(playheadTrail());
        juce::ColourGradient fade(trail.withAlpha(0.0f), x - width, 0.0f, trail.withAlpha(0.32f), x,
                                  0.0f, false);
        g.setGradientFill(fade);
        g.fillRect(juce::Rectangle<float>(x - width, top, width, bottom - top));
    }

    ThemedLookAndFeel::drawPlayhead(g, x, top, bottom, look);
}

} // namespace daw::ui::desktop
