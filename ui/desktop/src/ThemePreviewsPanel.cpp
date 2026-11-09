#include "daw/ui/desktop/ThemePreviewsPanel.h"

#include "daw/services/BuildInfo.h"

#include <cmath>

namespace daw::ui::desktop
{

namespace
{
using appearance::ColourRole;
using appearance::FontRole;

#if JUCE_MAC
constexpr const char* commandKey = "Cmd";
#else
constexpr const char* commandKey = "Ctrl";
#endif

constexpr int cardWidth = 300;
constexpr int cardHeight = 360;
constexpr int gap = 16;

// A waveform-like outline: decaying hits, the same in every theme.
juce::Path fakeWaveform(juce::Rectangle<float> area, int seed)
{
    juce::Path path;
    const float middle = area.getCentreY();
    const float half = area.getHeight() / 2.0f;
    const float period = 26.0f + static_cast<float>(seed) * 9.0f;

    for (float x = area.getX(); x <= area.getRight(); x += 1.0f)
    {
        const float phase = std::fmod(x - area.getX(), period) / period;
        const float envelope = std::exp(-5.0f * phase);
        const float wobble = 0.6f + 0.4f * std::sin(x * 0.9f + static_cast<float>(seed));
        const float y = half * envelope * wobble;
        if (x <= area.getX())
            path.startNewSubPath(x, middle - y);
        else
            path.lineTo(x, middle - y);
    }

    for (float x = area.getRight(); x >= area.getX(); x -= 1.0f)
    {
        const float phase = std::fmod(x - area.getX(), period) / period;
        const float envelope = std::exp(-5.0f * phase);
        path.lineTo(x, middle + half * envelope * 0.8f);
    }

    path.closeSubPath();
    return path;
}
} // namespace

// One theme: its miniature and summary, painted in that theme. Click to use it.
class ThemePreviewsPanel::Card final : public juce::Component
{
public:
    Card(ThemeManager& themeManager, const appearance::Theme& themeToShow)
        : themes(themeManager), theme(themeToShow)
    {
        setMouseCursor(juce::MouseCursor::PointingHandCursor);
        setTitle(theme.name);
        setDescription("Use the " + theme.name + " theme");
    }

    void paint(juce::Graphics& g) override
    {
        const auto& look = themes.getLookAndFeelFor(theme);
        auto area = getLocalBounds().toFloat().reduced(3.0f);

        // The card, in its own theme.
        const auto outline = look.shapeOf(area, CornerSize::large);
        g.setColour(look.colour(ColourRole::panel));
        g.fillPath(outline);

        auto content = area.reduced(12.0f);
        g.setColour(look.colour(ColourRole::text));
        g.setFont(look.font(FontRole::heading));
        g.drawText(ThemeFonts::textFor(theme.font(FontRole::heading), theme.name),
                   content.removeFromTop(22.0f), juce::Justification::centredLeft, false);
        content.removeFromTop(8.0f);

        // The miniature is painted once per size; the theme's images keep it.
        const auto miniature = content.removeFromTop(170.0f);
        const auto image = look.getImages().get(
            "preview", {miniature.toNearestInt().getWidth(), miniature.toNearestInt().getHeight()},
            [&look](juce::Graphics& imageGraphics, juce::Rectangle<int> size)
            { paintMiniature(imageGraphics, look, size.toFloat()); });
        g.setOpacity(1.0f);
        g.drawImageAt(image, juce::roundToInt(miniature.getX()),
                      juce::roundToInt(miniature.getY()));
        content.removeFromTop(12.0f);

        const auto line = [&](const juce::String& label, const juce::String& value)
        {
            auto row = content.removeFromTop(38.0f);
            g.setColour(look.colour(ColourRole::textSecondary));
            g.setFont(look.font(FontRole::caption));
            g.drawText(ThemeFonts::textFor(theme.font(FontRole::caption), label),
                       row.removeFromTop(15.0f), juce::Justification::centredLeft, false);
            g.setColour(look.colour(ColourRole::text));
            g.setFont(look.font(FontRole::label));
            g.drawFittedText(value, row.toNearestInt(), juce::Justification::topLeft, 2, 0.9f);
        };
        line("Fonts", theme.fontSummary());
        line("Shapes", theme.shapeSummary);
        line("Meters", theme.meterSummary);

        // The theme in use is marked in the panel's own theme.
        if (themes.getTheme().id == theme.id)
        {
            const auto& current = ThemedLookAndFeel::get(*this);
            g.setColour(current.colour(ColourRole::focus));
            g.strokePath(current.shapeOf(area.expanded(1.5f), CornerSize::large),
                         juce::PathStrokeType(3.0f));
        }
        else
        {
            g.setColour(look.colour(ColourRole::outline));
            g.strokePath(outline, juce::PathStrokeType(look.getTheme().shape.border));
        }
    }

    void mouseUp(const juce::MouseEvent& event) override
    {
        if (event.mouseWasClicked())
            themes.setTheme(theme.id);
    }

private:
    ThemeManager& themes;
    const appearance::Theme& theme;
};

ThemePreviewsPanel::ThemePreviewsPanel(ThemeManager& themeManager) : themes(themeManager)
{
    heading.setText("Themes", juce::dontSendNotification);
    ThemedLookAndFeel::setFontRole(heading, FontRole::title);
    addAndMakeVisible(heading);

    hint.setText(juce::String("Click a theme to use it (") + commandKey + "+1, " + commandKey +
                     "+2, " + commandKey + "+3). It applies at once, here too.",
                 juce::dontSendNotification);
    ThemedLookAndFeel::setColourRole(hint, ColourRole::textSecondary);
    addAndMakeVisible(hint);

    for (const auto& theme : themes.getLibrary().getThemes())
    {
        cards.push_back(std::make_unique<Card>(themes, theme));
        addAndMakeVisible(*cards.back());
    }

    themes.addListener(*this);
    const int count = static_cast<int>(cards.size());
    setSize(count * cardWidth + (count + 1) * gap, cardHeight + 96);
}

ThemePreviewsPanel::~ThemePreviewsPanel()
{
    themes.removeListener(*this);
}

void ThemePreviewsPanel::themeChanged()
{
    repaint();
}

void ThemePreviewsPanel::paint(juce::Graphics& g)
{
    ThemedLookAndFeel::get(*this).drawPanel(g, getLocalBounds().toFloat(), ColourRole::window,
                                            false);
}

void ThemePreviewsPanel::resized()
{
    auto area = getLocalBounds().reduced(gap);
    heading.setBounds(area.removeFromTop(28));
    hint.setBounds(area.removeFromTop(22));
    area.removeFromTop(gap / 2);

    for (auto& card : cards)
    {
        card->setBounds(area.removeFromLeft(cardWidth).withHeight(cardHeight));
        area.removeFromLeft(gap);
    }
}

void ThemePreviewsPanel::paintMiniature(juce::Graphics& g, const ThemedLookAndFeel& look,
                                        juce::Rectangle<float> area)
{
    look.drawPanel(g, area, ColourRole::window, false);

    // A transport strip with the logo and two buttons, one on.
    auto transport = area.removeFromTop(26.0f);
    look.drawPanel(g, transport, ColourRole::panel, true);
    transport.reduce(6.0f, 4.0f);
    look.drawLogo(g, transport.removeFromLeft(46.0f), services::buildInfo().logoText, 0.0, false);
    transport.removeFromLeft(8.0f);
    for (int index = 0; index < 2; ++index)
    {
        const auto button = transport.removeFromLeft(36.0f);
        transport.removeFromLeft(4.0f);
        const auto fill = look.colour(index == 0 ? ColourRole::raised : ColourRole::accent);
        g.setColour(fill);
        g.fillPath(look.shapeOf(button, CornerSize::small));
        g.setColour(look.colour(ColourRole::outline));
        g.strokePath(look.shapeOf(button, CornerSize::small),
                     juce::PathStrokeType(look.getTheme().shape.border));
        g.setColour(look.textColourOn(fill));
        g.setFont(look.font(FontRole::caption, 10.0f));
        g.drawText(index == 0 ? "Play" : "Loop", button, juce::Justification::centred, false);
    }

    // Three lanes with a clip each, a meter pair on the right.
    auto meters = area.removeFromRight(22.0f).reduced(4.0f, 6.0f);
    const float laneHeight = area.getHeight() / 3.0f;
    for (int row = 0; row < 3; ++row)
    {
        const auto lane =
            area.withY(area.getY() + laneHeight * static_cast<float>(row)).withHeight(laneHeight);
        look.drawLane(g, lane, row, true);

        const auto clip = lane.reduced(6.0f + 10.0f * static_cast<float>(row), 3.0f)
                              .withTrimmedRight(18.0f * static_cast<float>(2 - row));
        const auto style = look.clipStyle(row, ThemedLookAndFeel::ClipState::ready, row);
        look.drawClipFrame(g, clip, style);
        const float header = std::min(look.clipHeaderHeight(), clip.getHeight() / 3.0f);
        g.setColour(style.waveform);
        g.fillPath(fakeWaveform(clip.withTrimmedTop(header).reduced(2.0f, 3.0f), row));
        look.drawClipName(g, clip.withHeight(header).reduced(3.0f, 0.0f),
                          juce::StringArray{"drums", "bass", "pad"}[row], style);
        look.drawClipOutline(g, clip, style, row == 1);
    }

    look.drawPlayhead(g, area.getX() + area.getWidth() * 0.38f, area.getY(), area.getBottom(), {});

    const float barWidth = (meters.getWidth() - 2.0f) / 2.0f;
    look.drawMeterBar(g, meters.withWidth(barWidth), -5.0f, -2.0f, {});
    look.drawMeterBar(g, meters.withX(meters.getX() + barWidth + 2.0f).withWidth(barWidth), -11.0f,
                      -6.0f, {});
}

} // namespace daw::ui::desktop
