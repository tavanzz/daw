#include "ThemeLookAndFeels.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <vector>

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

// The logo's words: the product's name as CMakeLists.txt splits it ("MIC IN STATION").
juce::StringArray logoWords(const juce::String& text)
{
    auto words = juce::StringArray::fromTokens(text, " ", "");
    words.removeEmptyStrings();
    if (words.isEmpty())
        words.add(text);
    return words;
}

// Words in the title font, `height` high, their baseline at y = 0 and their left edge at
// x = 0; `tracking` spaces the letters apart.
juce::Path wordPath(const ThemedLookAndFeel& lookAndFeel, const juce::String& words, float height,
                    float tracking = 0.0f)
{
    const auto& spec = lookAndFeel.getTheme().font(FontRole::title);
    juce::GlyphArrangement glyphs;
    glyphs.addLineOfText(lookAndFeel.font(FontRole::title, height), ThemeFonts::textFor(spec, words), 0.0f, 0.0f);
    for (int index = 1; index < glyphs.getNumGlyphs(); ++index)
        glyphs.moveRangeOfGlyphs(index, 1, tracking * static_cast<float>(index), 0.0f);
    juce::Path path;
    glyphs.createPath(path);
    return path;
}

// Moves paths and rectangles together so that what they cover is centred in the area;
// returns what they cover.
juce::Rectangle<float> centreIn(juce::Rectangle<float> area, const std::vector<juce::Path*>& paths,
                                const std::vector<juce::Rectangle<float>*>& rectangles = {})
{
    std::optional<juce::Rectangle<float>> covered;
    const auto add = [&covered](juce::Rectangle<float> part)
    {
        if (!part.isEmpty())
            covered = covered ? covered->getUnion(part) : part;
    };
    for (const auto* path : paths)
        add(path->getBounds());
    for (const auto* rectangle : rectangles)
        add(*rectangle);
    if (!covered)
        return {};

    const auto shift = area.getCentre() - covered->getCentre();
    for (auto* path : paths)
        path->applyTransform(juce::AffineTransform::translation(shift.x, shift.y));
    for (auto* rectangle : rectangles)
        *rectangle += shift;
    return *covered + shift;
}

// Neon Grid: one line of neon tubes, the first word in the accent, the last in the second
// accent, those between smaller in the third.
struct NeonLogo
{
    std::vector<juce::Path> words;
    std::vector<ColourRole> colours;
    juce::Rectangle<float> bounds;
};

NeonLogo neonLogo(const ThemedLookAndFeel& lookAndFeel, juce::Rectangle<float> area, const juce::String& text)
{
    NeonLogo logo;
    const auto words = logoWords(text);
    const auto height = logoHeight(area);
    float x = 0.0f;
    for (int index = 0; index < words.size(); ++index)
    {
        const bool first = index == 0;
        const bool last = index == words.size() - 1;
        auto path = wordPath(lookAndFeel, words[index], first || last ? height : height * 0.7f);
        path.applyTransform(juce::AffineTransform::translation(x - path.getBounds().getX(), 0.0f));
        x = path.getBounds().getRight() + height * 0.22f;
        logo.words.push_back(std::move(path));
        logo.colours.push_back(first ? ColourRole::accent : last ? ColourRole::accentAlt : ColourRole::highlight);
    }
    std::vector<juce::Path*> paths;
    for (auto& word : logo.words)
        paths.push_back(&word);
    logo.bounds = centreIn(area, paths);
    return logo;
}

// Acid Terminal: a block in the accent with every word but the last cut out of it, like a
// terminal's banner or a jack's label, then the last word in the accent.
struct AcidLogo
{
    juce::Path banner;
    juce::Rectangle<float> block;
    juce::Path tail;
    juce::Rectangle<float> bounds;
};

AcidLogo acidLogo(const ThemedLookAndFeel& lookAndFeel, juce::Rectangle<float> area, const juce::String& text)
{
    AcidLogo logo;
    const auto words = logoWords(text);
    const auto height = logoHeight(area);
    const auto bannerWords = words.size() > 1 ? words.joinIntoString(" ", 0, words.size() - 1) : words[0];
    logo.banner = wordPath(lookAndFeel, bannerWords, height);
    logo.block = logo.banner.getBounds().expanded(8.0f, 4.0f);
    if (words.size() > 1)
    {
        logo.tail = wordPath(lookAndFeel, words[words.size() - 1], height);
        const auto left = logo.block.getRight() + height * 0.3f;
        logo.tail.applyTransform(juce::AffineTransform::translation(left - logo.tail.getBounds().getX(), 0.0f));
    }
    logo.bounds = centreIn(area, {&logo.banner, &logo.tail}, {&logo.block});
    return logo;
}

// Synth Chrome: stacked, the first word large in chrome, the last below it spaced out to
// the same width, and those between on the horizon line that separates them.
struct SynthLogo
{
    juce::Path top;
    juce::Path middle;
    juce::Path bottom;
    std::vector<juce::Rectangle<float>> horizon;
    juce::Rectangle<float> bounds;
};

SynthLogo synthLogo(const ThemedLookAndFeel& lookAndFeel, juce::Rectangle<float> area, const juce::String& text)
{
    // Laid out at a nominal size, then scaled to the area's height.
    constexpr float large = 20.0f;
    constexpr float small = 8.5f;
    constexpr float gap = 3.0f;
    constexpr float line = 1.6f;

    SynthLogo logo;
    const auto words = logoWords(text);
    logo.top = wordPath(lookAndFeel, words[0], large);
    const auto topBounds = logo.top.getBounds();
    logo.top.applyTransform(juce::AffineTransform::translation(-topBounds.getX(), -topBounds.getY()));
    const auto width = topBounds.getWidth();
    const auto lineY = topBounds.getHeight() + gap;

    if (words.size() > 2)
    {
        logo.middle = wordPath(lookAndFeel, words.joinIntoString(" ", 1, words.size() - 2), small);
        const auto bounds = logo.middle.getBounds();
        logo.middle.applyTransform(juce::AffineTransform::translation(
            (width - bounds.getWidth()) / 2.0f - bounds.getX(), lineY + line / 2.0f - bounds.getCentreY()));
        const auto placed = logo.middle.getBounds();
        logo.horizon.push_back({0.0f, lineY, std::max(0.0f, placed.getX() - gap), line});
        logo.horizon.push_back({placed.getRight() + gap, lineY, std::max(0.0f, width - placed.getRight() - gap), line});
    }
    else
        logo.horizon.push_back({0.0f, lineY, width, line});

    if (words.size() > 1)
    {
        // Spaced out to the top word's width.
        const auto& last = words[words.size() - 1];
        const auto letters = ThemeFonts::textFor(lookAndFeel.getTheme().font(FontRole::title), last).length();
        const auto plain = wordPath(lookAndFeel, last, small).getBounds().getWidth();
        const auto tracking = letters > 1 ? std::max(0.0f, (width - plain) / static_cast<float>(letters - 1)) : 0.0f;
        logo.bottom = wordPath(lookAndFeel, last, small, tracking);
        const auto bounds = logo.bottom.getBounds();
        const auto top = lineY + std::max(line, logo.middle.isEmpty() ? 0.0f : logo.middle.getBounds().getHeight() / 2.0f) + gap;
        logo.bottom.applyTransform(
            juce::AffineTransform::translation((width - bounds.getWidth()) / 2.0f - bounds.getX(), top - bounds.getY()));
    }

    // Scaled to the area's height (a little less), then centred.
    auto height = lineY + line;
    for (const auto* path : {&logo.middle, &logo.bottom})
        if (!path->isEmpty())
            height = std::max(height, path->getBounds().getBottom());
    const auto scale = area.getHeight() * 0.86f / height;
    const auto scaling = juce::AffineTransform::scale(scale);
    for (auto* path : {&logo.top, &logo.middle, &logo.bottom})
        path->applyTransform(scaling);
    std::vector<juce::Rectangle<float>*> lines;
    for (auto& segment : logo.horizon)
    {
        segment = segment.transformedBy(scaling);
        lines.push_back(&segment);
    }
    logo.bounds = centreIn(area, {&logo.top, &logo.middle, &logo.bottom}, lines);
    return logo;
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

    const auto logo = neonLogo(*this, area, text);
    for (std::size_t index = 0; index < logo.words.size(); ++index)
    {
        const auto tube = colour(logo.colours[index]);
        drawGlow(g, logo.words[index], tube, brightness);
        g.setColour(tube.withMultipliedAlpha(0.35f + 0.65f * brightness));
        g.fillPath(logo.words[index]);
    }
}

float NeonGridLookAndFeel::logoWidth(const juce::String& text, float height) const
{
    return neonLogo(*this, {0.0f, 0.0f, 1000.0f, height}, text).bounds.getWidth() + 6.0f;
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
    const auto logo = acidLogo(*this, area, text);
    const auto ink = textColourOn(colour(ColourRole::accent));
    const auto draw = [this, &logo, ink](juce::Graphics& graphics, bool ghost)
    {
        graphics.setColour(colour(ColourRole::accent));
        graphics.fillRect(logo.block);
        graphics.fillPath(logo.tail);
        if (ghost)
        {
            graphics.setColour(colour(ColourRole::accentAlt).withAlpha(0.8f));
            graphics.fillPath(logo.banner, juce::AffineTransform::translation(2.0f, 0.0f));
        }
        graphics.setColour(ink);
        graphics.fillPath(logo.banner);
    };

    // For a moment every few seconds the logo glitches: slices of it jump sideways and a
    // ghost of the banner's words in the second accent slips out.
    const double t = std::fmod(seconds, 3.3);
    if (!glitching || t >= 0.16)
    {
        draw(g, false);
        return;
    }

    const int step = static_cast<int>(t * 50.0);
    const float slice = logo.block.getHeight() / 3.0f;
    for (int index = 0; index < 3; ++index)
    {
        const float shift = static_cast<float>(((step + index * 2) % 5) - 2) * 2.0f;
        const auto band = logo.bounds.withY(logo.block.getY() + slice * static_cast<float>(index)).withHeight(slice);

        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(band.expanded(8.0f, 0.0f).toNearestInt());
        g.addTransform(juce::AffineTransform::translation(shift, 0.0f));
        draw(g, true);
    }
}

float AcidTerminalLookAndFeel::logoWidth(const juce::String& text, float height) const
{
    return acidLogo(*this, {0.0f, 0.0f, 1000.0f, height}, text).bounds.getWidth() + 6.0f;
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
    // Chrome letters on top: light at the top, a dark band at their horizon, light again
    // below; under them the horizon line in the accent, the middle words on it, and the
    // last word in the second accent.
    const auto logo = synthLogo(*this, area, text);
    const auto bounds = logo.top.getBounds();
    const auto light = colour(ColourRole::text);
    const auto dark = colour(ColourRole::textSecondary);

    juce::ColourGradient chrome(light, 0.0f, bounds.getY(), light, 0.0f, bounds.getBottom(), false);
    chrome.addColour(0.48, dark);
    chrome.addColour(0.52, colour(ColourRole::accentAlt));
    g.setGradientFill(chrome);
    g.fillPath(logo.top);

    g.setColour(colour(ColourRole::accent));
    for (const auto& segment : logo.horizon)
        g.fillRect(segment);
    g.fillPath(logo.middle);
    g.setColour(colour(ColourRole::accentAlt));
    g.fillPath(logo.bottom);

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
    g.reduceClipRegion(logo.top);
    g.setColour(colour(ColourRole::text).withAlpha(0.85f));
    g.fillPath(streak);
}

float SynthChromeLookAndFeel::logoWidth(const juce::String& text, float height) const
{
    return synthLogo(*this, {0.0f, 0.0f, 1000.0f, height}, text).bounds.getWidth() + 6.0f;
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
