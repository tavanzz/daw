#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include "ThemeLookAndFeels.h"
#include "daw/appearance/ContrastRules.h"
#include "daw/appearance/ThemeLibrary.h"
#include "daw/engine/dsp/MeterBallistics.h"

#include <algorithm>
#include <cmath>
#include <optional>

namespace daw::ui::desktop
{

namespace
{
using appearance::ColourRole;
using appearance::CornerStyle;
using appearance::FontRole;

constexpr float disabledAlpha = 0.45f;

std::optional<ColourRole> colourRoleNamed(const juce::var& value)
{
    if (!value.isString())
        return std::nullopt;

    for (std::size_t index = 0; index < appearance::colourRoleCount; ++index)
    {
        const auto role = static_cast<ColourRole>(index);
        if (value.toString() == juce::String(std::string(appearance::keyOf(role))))
            return role;
    }

    return std::nullopt;
}

juce::String keyString(ColourRole role)
{
    return juce::String(std::string(appearance::keyOf(role)));
}

// The look and feel of components outside the app (tests that made no ThemeManager),
// freed with JUCE's other singletons.
class FallbackLookAndFeel final : private juce::DeletedAtShutdown
{
public:
    static ThemedLookAndFeel& get()
    {
        if (instance == nullptr)
            instance = new FallbackLookAndFeel();
        return *instance->lookAndFeel;
    }

    ~FallbackLookAndFeel() override { instance = nullptr; }

private:
    FallbackLookAndFeel() = default;

    static inline FallbackLookAndFeel* instance = nullptr;
    ThemeFonts fonts;
    std::unique_ptr<ThemedLookAndFeel> lookAndFeel =
        ThemedLookAndFeel::make(appearance::ThemeLibrary::builtIn().getDefault(), fonts);
};

juce::LookAndFeel_V4::ColourScheme schemeFor(const appearance::Theme& theme)
{
    const auto c = [&theme](ColourRole role)
    {
        return toJuce(theme.colour(role));
    };

    return {c(ColourRole::window),
            c(ColourRole::raised),
            c(ColourRole::raised),
            c(ColourRole::outline),
            c(ColourRole::text),
            c(ColourRole::accent),
            toJuce(theme.colour(appearance::textRoleOn(theme, theme.colour(ColourRole::accent)))),
            c(ColourRole::accent),
            c(ColourRole::text)};
}
} // namespace

ThemedLookAndFeel::ThemedLookAndFeel(const appearance::Theme& themeToDraw,
                                     const ThemeFonts& fontsToUse)
    : juce::LookAndFeel_V4(schemeFor(themeToDraw)), theme(themeToDraw), fonts(fontsToUse)
{
    setColoursFromTheme();
}

std::unique_ptr<ThemedLookAndFeel> ThemedLookAndFeel::make(const appearance::Theme& themeToDraw,
                                                           const ThemeFonts& fontsToUse)
{
    if (themeToDraw.id == "neon-grid")
        return std::make_unique<NeonGridLookAndFeel>(themeToDraw, fontsToUse);
    if (themeToDraw.id == "acid-terminal")
        return std::make_unique<AcidTerminalLookAndFeel>(themeToDraw, fontsToUse);
    if (themeToDraw.id == "synth-chrome")
        return std::make_unique<SynthChromeLookAndFeel>(themeToDraw, fontsToUse);

    return std::make_unique<ThemedLookAndFeel>(themeToDraw, fontsToUse);
}

ThemedLookAndFeel* ThemedLookAndFeel::of(const juce::Component& component) noexcept
{
    // JUCE's getLookAndFeel() is not const, though it changes nothing.
    return dynamic_cast<ThemedLookAndFeel*>(
        &const_cast<juce::Component&>(component).getLookAndFeel());
}

ThemedLookAndFeel& ThemedLookAndFeel::get(const juce::Component& component)
{
    if (auto* themed = of(component))
        return *themed;

    return FallbackLookAndFeel::get();
}

void ThemedLookAndFeel::setFontRole(juce::Component& component, FontRole role)
{
    component.getProperties().set(fontRoleProperty,
                                  juce::String(std::string(appearance::keyOf(role))));
    component.repaint();
}

void ThemedLookAndFeel::setColourRole(juce::Component& component, ColourRole role)
{
    if (component.getProperties()[colourRoleProperty].toString() == keyString(role))
        return;

    component.getProperties().set(colourRoleProperty, keyString(role));
    component.repaint();
}

void ThemedLookAndFeel::setSurface(juce::Component& component, ColourRole role)
{
    component.getProperties().set(surfaceProperty, keyString(role));
    component.repaint();
}

std::optional<juce::Colour> ThemedLookAndFeel::buttonOnColour(const juce::Button& button) const
{
    if (const auto role = colourRoleNamed(button.getProperties()[colourRoleProperty]))
        return colour(*role);

    return std::nullopt;
}

juce::Colour ThemedLookAndFeel::colour(ColourRole role) const noexcept
{
    return toJuce(theme.colour(role));
}

juce::Font ThemedLookAndFeel::font(FontRole role) const
{
    return fonts.font(theme.font(role));
}

juce::Font ThemedLookAndFeel::font(FontRole role, float height) const
{
    return fonts.font(theme.font(role), height);
}

juce::Colour ThemedLookAndFeel::textColourOn(juce::Colour fill) const noexcept
{
    const appearance::Colour themeFill{fill.getRed(), fill.getGreen(), fill.getBlue(),
                                       fill.getAlpha()};
    return colour(appearance::textRoleOn(theme, themeFill));
}

float ThemedLookAndFeel::cornerSize(CornerSize size) const noexcept
{
    switch (size)
    {
    case CornerSize::small:
        return theme.shape.cornerSmall;
    case CornerSize::medium:
        return theme.shape.cornerMedium;
    case CornerSize::large:
        return theme.shape.cornerLarge;
    }

    return 0.0f;
}

juce::Path ThemedLookAndFeel::shapeOf(juce::Rectangle<float> area, CornerSize size) const
{
    juce::Path path;
    const float corner =
        std::min({cornerSize(size), area.getWidth() / 2.0f, area.getHeight() / 2.0f});

    if (theme.shape.corners == CornerStyle::sharp || corner <= 0.0f)
    {
        path.addRectangle(area);
    }
    else if (theme.shape.corners == CornerStyle::rounded)
    {
        path.addRoundedRectangle(area, corner);
    }
    else
    {
        path.startNewSubPath(area.getX() + corner, area.getY());
        path.lineTo(area.getRight(), area.getY());
        path.lineTo(area.getRight(), area.getBottom() - corner);
        path.lineTo(area.getRight() - corner, area.getBottom());
        path.lineTo(area.getX(), area.getBottom());
        path.lineTo(area.getX(), area.getY() + corner);
        path.closeSubPath();
    }

    return path;
}

void ThemedLookAndFeel::setColoursFromTheme()
{
    const auto c = [this](ColourRole role)
    {
        return colour(role);
    };
    const auto onAccent = textColourOn(c(ColourRole::accent));

    setColour(juce::ResizableWindow::backgroundColourId, c(ColourRole::window));
    setColour(juce::DocumentWindow::textColourId, c(ColourRole::text));

    setColour(juce::TextButton::buttonColourId, c(ColourRole::raised));
    setColour(juce::TextButton::buttonOnColourId, c(ColourRole::accent));
    setColour(juce::TextButton::textColourOffId, c(ColourRole::text));
    setColour(juce::TextButton::textColourOnId, onAccent);
    setColour(juce::ToggleButton::textColourId, c(ColourRole::text));
    setColour(juce::ToggleButton::tickColourId, c(ColourRole::accent));
    setColour(juce::ToggleButton::tickDisabledColourId, c(ColourRole::textDisabled));
    setColour(juce::HyperlinkButton::textColourId, c(ColourRole::accentAlt));

    setColour(juce::Label::textColourId, c(ColourRole::text));
    setColour(juce::Label::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    setColour(juce::Label::textWhenEditingColourId, c(ColourRole::text));
    setColour(juce::Label::backgroundWhenEditingColourId, c(ColourRole::inset));
    setColour(juce::Label::outlineWhenEditingColourId, c(ColourRole::focus));

    setColour(juce::TextEditor::backgroundColourId, c(ColourRole::inset));
    setColour(juce::TextEditor::textColourId, c(ColourRole::text));
    setColour(juce::TextEditor::highlightColourId, c(ColourRole::accent).withAlpha(0.35f));
    setColour(juce::TextEditor::highlightedTextColourId, c(ColourRole::text));
    setColour(juce::TextEditor::outlineColourId, c(ColourRole::outline));
    setColour(juce::TextEditor::focusedOutlineColourId, c(ColourRole::focus));
    setColour(juce::CaretComponent::caretColourId, c(ColourRole::accent));

    setColour(juce::ComboBox::backgroundColourId, c(ColourRole::raised));
    setColour(juce::ComboBox::textColourId, c(ColourRole::text));
    setColour(juce::ComboBox::outlineColourId, c(ColourRole::outline));
    setColour(juce::ComboBox::arrowColourId, c(ColourRole::textSecondary));
    setColour(juce::ComboBox::buttonColourId, c(ColourRole::raised));
    setColour(juce::ComboBox::focusedOutlineColourId, c(ColourRole::focus));

    setColour(juce::PopupMenu::backgroundColourId, c(ColourRole::raised));
    setColour(juce::PopupMenu::textColourId, c(ColourRole::text));
    setColour(juce::PopupMenu::headerTextColourId, c(ColourRole::textSecondary));
    setColour(juce::PopupMenu::highlightedBackgroundColourId, c(ColourRole::accent));
    setColour(juce::PopupMenu::highlightedTextColourId, onAccent);

    setColour(juce::Slider::backgroundColourId, c(ColourRole::inset));
    setColour(juce::Slider::trackColourId, c(ColourRole::accent));
    setColour(juce::Slider::thumbColourId, c(ColourRole::text));
    setColour(juce::Slider::rotarySliderFillColourId, c(ColourRole::accent));
    setColour(juce::Slider::rotarySliderOutlineColourId, c(ColourRole::inset));
    setColour(juce::Slider::textBoxTextColourId, c(ColourRole::text));
    setColour(juce::Slider::textBoxBackgroundColourId, c(ColourRole::inset));
    setColour(juce::Slider::textBoxHighlightColourId, c(ColourRole::accent).withAlpha(0.35f));
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);

    setColour(juce::ScrollBar::backgroundColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::trackColourId, juce::Colours::transparentBlack);
    setColour(juce::ScrollBar::thumbColourId, c(ColourRole::outline));

    setColour(juce::TooltipWindow::backgroundColourId, c(ColourRole::raised));
    setColour(juce::TooltipWindow::textColourId, c(ColourRole::text));
    setColour(juce::TooltipWindow::outlineColourId, c(ColourRole::outline));

    setColour(juce::AlertWindow::backgroundColourId, c(ColourRole::panel));
    setColour(juce::AlertWindow::textColourId, c(ColourRole::text));
    setColour(juce::AlertWindow::outlineColourId, c(ColourRole::outline));

    setColour(juce::ProgressBar::backgroundColourId, c(ColourRole::inset));
    setColour(juce::ProgressBar::foregroundColourId, c(ColourRole::accent));

    setColour(juce::ListBox::backgroundColourId, c(ColourRole::inset));
    setColour(juce::ListBox::outlineColourId, c(ColourRole::outline));
    setColour(juce::ListBox::textColourId, c(ColourRole::text));

    setColour(juce::GroupComponent::outlineColourId, c(ColourRole::outline));
    setColour(juce::GroupComponent::textColourId, c(ColourRole::textSecondary));

    setColour(juce::TreeView::backgroundColourId, c(ColourRole::inset));
    setColour(juce::BubbleComponent::backgroundColourId, c(ColourRole::raised));
    setColour(juce::BubbleComponent::outlineColourId, c(ColourRole::outline));
}

void ThemedLookAndFeel::drawGlow(juce::Graphics& g, const juce::Path& shape,
                                 juce::Colour glowColour, float opacity) const
{
    const float radius = theme.shape.glow;
    if (radius <= 0.0f || shape.isEmpty())
        return;

    // The blurred outline of the shape, in an image with room for the glow around it.
    // The image is made for the shape moved to the origin, so the same shape anywhere
    // (a moving playhead, buttons of one size) reuses it.
    const auto origin = shape.getBounds().getPosition();
    auto atOrigin = shape;
    atOrigin.applyTransform(juce::AffineTransform::translation(-origin.x, -origin.y));
    const auto size = atOrigin.getBounds().expanded(radius).getSmallestIntegerContainer();
    const auto key = "glow/" + glowColour.toString() + "/" +
                     juce::String(static_cast<juce::int64>(atOrigin.toString().hashCode64()));

    const auto image = images.get(
        key, {size.getWidth(), size.getHeight()},
        [&](juce::Graphics& imageGraphics, juce::Rectangle<int>)
        {
            juce::Path tube;
            juce::PathStrokeType(theme.shape.border * 2.0f).createStrokedPath(tube, atOrigin);
            tube.applyTransform(juce::AffineTransform::translation(
                static_cast<float>(-size.getX()), static_cast<float>(-size.getY())));
            juce::DropShadow(glowColour.withMultipliedAlpha(0.85f), static_cast<int>(radius), {})
                .drawForPath(imageGraphics, tube);
        });

    g.setOpacity(opacity);
    g.drawImageAt(image, juce::roundToInt(origin.x) + size.getX(),
                  juce::roundToInt(origin.y) + size.getY());
    g.setOpacity(1.0f);
}

//==============================================================================
void ThemedLookAndFeel::drawControlOutline(juce::Graphics& g, const juce::Path& outline,
                                           bool isActive, juce::Colour activeColour) const
{
    if (isActive)
        drawGlow(g, outline, activeColour);

    g.setColour(isActive ? activeColour : colour(ColourRole::outline));
    g.strokePath(outline, juce::PathStrokeType(theme.shape.border));
}

void ThemedLookAndFeel::drawButtonBackground(juce::Graphics& g, juce::Button& button,
                                             const juce::Colour& backgroundColour,
                                             bool isMouseOverButton, bool isButtonDown)
{
    const float inset = theme.shape.border / 2.0f;
    const auto area = button.getLocalBounds().toFloat().reduced(inset);
    const auto outline = shapeOf(area, CornerSize::small);
    const bool isOn = button.getToggleState();
    const auto onColour = buttonOnColour(button);
    const auto baseColour = isOn && onColour ? *onColour : backgroundColour;

    auto fill = baseColour;
    if (isButtonDown)
        fill = fill.interpolatedWith(colour(ColourRole::text), 0.18f);
    else if (isMouseOverButton)
        fill = fill.interpolatedWith(colour(ColourRole::text), 0.08f);
    if (!button.isEnabled())
        fill = fill.withMultipliedAlpha(disabledAlpha);

    g.setColour(fill);
    g.fillPath(outline);

    drawControlOutline(g, outline, isOn, baseColour);

    if (const auto pulse = static_cast<float>(button.getProperties()[pulseProperty]);
        isOn && pulse > 0.0f)
        drawRecordPulse(g, area, baseColour, pulse);

    if (button.hasKeyboardFocus(true))
    {
        g.setColour(colour(ColourRole::focus));
        g.strokePath(shapeOf(button.getLocalBounds().toFloat().reduced(theme.shape.focus / 2.0f),
                             CornerSize::small),
                     juce::PathStrokeType(theme.shape.focus));
    }
}

juce::Font ThemedLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    const auto& spec = theme.font(FontRole::label);
    return fonts.font(spec, std::min(spec.size, static_cast<float>(buttonHeight) * 0.7f));
}

void ThemedLookAndFeel::drawButtonText(juce::Graphics& g, juce::TextButton& button, bool, bool)
{
    const auto buttonFont = getTextButtonFont(button, button.getHeight());
    const bool isOn = button.getToggleState();
    auto textColour = button.findColour(isOn ? juce::TextButton::textColourOnId
                                             : juce::TextButton::textColourOffId);

    // Buttons filled with a status colour when on get the text colour that reads on it.
    if (const auto onColour = buttonOnColour(button); isOn && onColour)
        textColour = textColourOn(*onColour);
    else if (isOn && button.isColourSpecified(juce::TextButton::buttonOnColourId))
        textColour = textColourOn(button.findColour(juce::TextButton::buttonOnColourId));

    g.setFont(buttonFont);
    g.setColour(textColour.withMultipliedAlpha(button.isEnabled() ? 1.0f : disabledAlpha));

    const int padding = std::max(2, button.getHeight() / 5);
    g.drawFittedText(labelText(FontRole::label, button.getButtonText()),
                     button.getLocalBounds().reduced(padding, 0), juce::Justification::centred, 1);
}

void ThemedLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                         bool isMouseOverButton, bool isButtonDown)
{
    const auto labelFont = fonts.font(theme.font(FontRole::label));
    const float tickSize = std::min(16.0f, static_cast<float>(button.getHeight()) - 4.0f);
    const float tickY = (static_cast<float>(button.getHeight()) - tickSize) / 2.0f;

    drawTickBox(g, button, 2.0f, tickY, tickSize, tickSize, button.getToggleState(),
                button.isEnabled(), isMouseOverButton, isButtonDown);

    g.setFont(labelFont);
    g.setColour(colour(button.isEnabled() ? ColourRole::text : ColourRole::textDisabled));

    const auto textArea = button.getLocalBounds().withTrimmedLeft(static_cast<int>(tickSize) + 8);
    g.drawFittedText(labelText(FontRole::label, button.getButtonText()), textArea,
                     juce::Justification::centredLeft, 2);
}

void ThemedLookAndFeel::drawTickBox(juce::Graphics& g, juce::Component& component, float x, float y,
                                    float w, float h, bool ticked, bool isEnabled,
                                    bool isMouseOverButton, bool)
{
    const juce::Rectangle<float> box(x, y, w, h);
    const auto outline = shapeOf(box.reduced(theme.shape.border / 2.0f), CornerSize::small);

    g.setColour(colour(ColourRole::inset));
    g.fillPath(outline);
    drawControlOutline(g, outline, ticked || isMouseOverButton, colour(ColourRole::accent));

    if (ticked)
    {
        g.setColour(isEnabled ? colour(ColourRole::accent) : colour(ColourRole::textDisabled));
        g.fillPath(shapeOf(box.reduced(w * 0.28f), CornerSize::small));
    }

    if (component.hasKeyboardFocus(false))
    {
        g.setColour(colour(ColourRole::focus));
        g.strokePath(shapeOf(box.expanded(1.0f), CornerSize::small),
                     juce::PathStrokeType(theme.shape.focus / 2.0f));
    }
}

//==============================================================================
FontRole ThemedLookAndFeel::roleOf(const juce::Label& label) const
{
    const auto name = label.getProperties()[fontRoleProperty].toString();

    for (std::size_t index = 0; index < appearance::fontRoleCount; ++index)
    {
        const auto role = static_cast<FontRole>(index);
        if (name == juce::String(std::string(appearance::keyOf(role))))
            return role;
    }

    return FontRole::label;
}

juce::String ThemedLookAndFeel::labelText(FontRole role, const juce::String& text) const
{
    return ThemeFonts::textFor(theme.font(role), text);
}

juce::Font ThemedLookAndFeel::getLabelFont(juce::Label& label)
{
    return fonts.font(theme.font(roleOf(label)));
}

void ThemedLookAndFeel::drawLabel(juce::Graphics& g, juce::Label& label)
{
    const auto surface = colourRoleNamed(label.getProperties()[surfaceProperty]);

    if (surface)
    {
        const auto outline = shapeOf(
            label.getLocalBounds().toFloat().reduced(theme.shape.border / 2.0f), CornerSize::small);
        g.setColour(colour(*surface));
        g.fillPath(outline);
        g.setColour(colour(label.isBeingEdited() ? ColourRole::focus : ColourRole::outline));
        g.strokePath(outline, juce::PathStrokeType(theme.shape.border));
    }
    else
    {
        g.fillAll(label.findColour(juce::Label::backgroundColourId));
    }

    if (!label.isBeingEdited())
    {
        const auto role = roleOf(label);
        const float alpha = label.isEnabled() ? 1.0f : disabledAlpha;
        const auto textArea = getLabelBorderSize(label).subtractedFrom(label.getLocalBounds());
        const auto textRole = colourRoleNamed(label.getProperties()[colourRoleProperty]);
        const auto textColour =
            textRole ? colour(*textRole) : label.findColour(juce::Label::textColourId);

        g.setColour(textColour.withMultipliedAlpha(alpha));
        g.setFont(getLabelFont(label));
        g.drawFittedText(labelText(role, label.getText()), textArea, label.getJustificationType(),
                         std::max(1, static_cast<int>(static_cast<float>(textArea.getHeight()) /
                                                      g.getCurrentFont().getHeight())),
                         label.getMinimumHorizontalScale());
    }

    if (!surface)
    {
        g.setColour(label.findColour(label.isBeingEdited() ? juce::Label::outlineWhenEditingColourId
                                                           : juce::Label::outlineColourId));
        g.drawRect(label.getLocalBounds());
    }
}

//==============================================================================
void ThemedLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float minSliderPos, float maxSliderPos,
                                         juce::Slider::SliderStyle style, juce::Slider& slider)
{
    if (slider.isBar() || style == juce::Slider::TwoValueHorizontal ||
        style == juce::Slider::TwoValueVertical || style == juce::Slider::ThreeValueHorizontal ||
        style == juce::Slider::ThreeValueVertical)
    {
        juce::LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, minSliderPos,
                                               maxSliderPos, style, slider);
        return;
    }

    const bool vertical = slider.isVertical();
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat();
    const float trackWidth = vertical ? std::min(6.0f, area.getWidth() * 0.25f)
                                      : std::min(6.0f, area.getHeight() * 0.25f);

    const auto track =
        vertical ? juce::Rectangle<float>(trackWidth, area.getHeight()).withCentre(area.getCentre())
                 : juce::Rectangle<float>(area.getWidth(), trackWidth).withCentre(area.getCentre());

    g.setColour(slider.findColour(juce::Slider::backgroundColourId));
    g.fillPath(shapeOf(track, CornerSize::small));

    const auto filled = vertical ? track.withTop(sliderPos) : track.withRight(sliderPos);
    g.setColour(slider.findColour(juce::Slider::trackColourId)
                    .withMultipliedAlpha(slider.isEnabled() ? 1.0f : disabledAlpha));
    g.fillPath(shapeOf(filled, CornerSize::small));

    // The thumb: a bar across the track, the theme's shape.
    const float thumbLength = 10.0f;
    const float thumbBreadth = std::min(vertical ? area.getWidth() : area.getHeight(), 22.0f);
    const auto thumb = vertical ? juce::Rectangle<float>(thumbBreadth, thumbLength)
                                      .withCentre({area.getCentreX(), sliderPos})
                                : juce::Rectangle<float>(thumbLength, thumbBreadth)
                                      .withCentre({sliderPos, area.getCentreY()});
    const auto thumbShape = shapeOf(thumb, CornerSize::small);

    g.setColour(colour(ColourRole::raised));
    g.fillPath(thumbShape);
    drawControlOutline(g, thumbShape, slider.isMouseOverOrDragging(),
                       slider.findColour(juce::Slider::trackColourId));

    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    const auto grip =
        vertical ? juce::Rectangle<float>(thumbBreadth * 0.6f, 1.5f).withCentre(thumb.getCentre())
                 : juce::Rectangle<float>(1.5f, thumbBreadth * 0.6f).withCentre(thumb.getCentre());
    g.fillRect(grip);
}

void ThemedLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPosProportional, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    const auto area = juce::Rectangle<int>(x, y, width, height).toFloat().reduced(2.0f);
    const float radius = std::min(area.getWidth(), area.getHeight()) / 2.0f;
    const auto centre = area.getCentre();
    const float lineWidth = std::max(2.0f, radius * 0.18f);
    const float arcRadius = radius - lineWidth / 2.0f;
    const float angle =
        rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);

    // Bipolar controls (pan) light from the centre of their range, others from the start.
    const bool bipolar = slider.getMinimum() < 0.0 && slider.getMaximum() > 0.0;
    const float fromAngle = bipolar ? (rotaryStartAngle + rotaryEndAngle) / 2.0f : rotaryStartAngle;

    juce::Path background;
    background.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, rotaryStartAngle,
                             rotaryEndAngle, true);
    g.setColour(slider.findColour(juce::Slider::rotarySliderOutlineColourId));
    g.strokePath(background, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                                  juce::PathStrokeType::butt));

    if (std::abs(angle - fromAngle) > 0.001f)
    {
        juce::Path value;
        value.addCentredArc(centre.x, centre.y, arcRadius, arcRadius, 0.0f, fromAngle, angle, true);
        g.setColour(slider.findColour(juce::Slider::rotarySliderFillColourId)
                        .withMultipliedAlpha(slider.isEnabled() ? 1.0f : disabledAlpha));
        g.strokePath(value, juce::PathStrokeType(lineWidth, juce::PathStrokeType::curved,
                                                 juce::PathStrokeType::butt));
    }

    if (theme.shape.pan == appearance::PanStyle::knob)
    {
        const float knobRadius = arcRadius - lineWidth;
        g.setColour(colour(ColourRole::raised));
        g.fillEllipse(
            juce::Rectangle<float>(knobRadius * 2.0f, knobRadius * 2.0f).withCentre(centre));
        g.setColour(colour(ColourRole::outline));
        g.drawEllipse(
            juce::Rectangle<float>(knobRadius * 2.0f, knobRadius * 2.0f).withCentre(centre),
            theme.shape.border);
    }

    // The pointer.
    const auto tip = centre.getPointOnCircumference(arcRadius - lineWidth, angle);
    const auto base = centre.getPointOnCircumference(
        theme.shape.pan == appearance::PanStyle::knob ? 0.0f : arcRadius * 0.45f, angle);
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.drawLine({base, tip}, std::max(1.5f, lineWidth * 0.6f));
}

juce::Label* ThemedLookAndFeel::createSliderTextBox(juce::Slider& slider)
{
    auto* label = juce::LookAndFeel_V4::createSliderTextBox(slider);
    label->getProperties().set(fontRoleProperty,
                               juce::String(std::string(appearance::keyOf(FontRole::numeric))));
    label->setColour(juce::Label::outlineColourId, juce::Colours::transparentBlack);
    return label;
}

juce::Font ThemedLookAndFeel::getSliderPopupFont(juce::Slider&)
{
    return font(FontRole::numeric);
}

//==============================================================================
void ThemedLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool, int, int, int,
                                     int, juce::ComboBox& box)
{
    const auto area =
        juce::Rectangle<int>(width, height).toFloat().reduced(theme.shape.border / 2.0f);
    const auto outline = shapeOf(area, CornerSize::small);

    g.setColour(box.findColour(juce::ComboBox::backgroundColourId)
                    .withMultipliedAlpha(box.isEnabled() ? 1.0f : disabledAlpha));
    g.fillPath(outline);
    drawControlOutline(g, outline, box.hasKeyboardFocus(true),
                       box.findColour(juce::ComboBox::focusedOutlineColourId));

    const auto arrowZone = juce::Rectangle<float>(static_cast<float>(width) - 20.0f, 0.0f, 14.0f,
                                                  static_cast<float>(height));
    juce::Path arrow;
    arrow.startNewSubPath(arrowZone.getX() + 3.0f, arrowZone.getCentreY() - 2.0f);
    arrow.lineTo(arrowZone.getCentreX(), arrowZone.getCentreY() + 2.5f);
    arrow.lineTo(arrowZone.getRight() - 3.0f, arrowZone.getCentreY() - 2.0f);

    g.setColour(box.findColour(juce::ComboBox::arrowColourId)
                    .withMultipliedAlpha(box.isEnabled() ? 1.0f : disabledAlpha));
    g.strokePath(arrow, juce::PathStrokeType(1.6f));
}

juce::Font ThemedLookAndFeel::getComboBoxFont(juce::ComboBox& box)
{
    const auto& spec = theme.font(FontRole::label);
    return fonts.font(spec, std::min(spec.size, static_cast<float>(box.getHeight()) * 0.7f));
}

void ThemedLookAndFeel::positionComboBoxText(juce::ComboBox& box, juce::Label& label)
{
    label.setBounds(1, 1, box.getWidth() - 22, box.getHeight() - 2);
    label.setFont(getComboBoxFont(box));
}

//==============================================================================
void ThemedLookAndFeel::drawPopupMenuBackground(juce::Graphics& g, int width, int height)
{
    const auto area = juce::Rectangle<int>(width, height).toFloat();
    g.fillAll(colour(ColourRole::raised));
    g.setColour(colour(ColourRole::outline));
    g.drawRect(area, theme.shape.border);
}

void ThemedLookAndFeel::drawPopupMenuItem(juce::Graphics& g, const juce::Rectangle<int>& area,
                                          bool isSeparator, bool isActive, bool isHighlighted,
                                          bool isTicked, bool hasSubMenu, const juce::String& text,
                                          const juce::String& shortcutKeyText,
                                          const juce::Drawable* icon,
                                          const juce::Colour* textColour)
{
    if (isSeparator)
    {
        auto line = area.reduced(6, 0).toFloat();
        line = line.withSizeKeepingCentre(line.getWidth(), 1.0f);
        g.setColour(colour(ColourRole::separator));
        g.fillRect(line);
        return;
    }

    auto r = area.reduced(1);
    auto foreground = textColour != nullptr ? *textColour : colour(ColourRole::text);

    if (isHighlighted && isActive)
    {
        g.setColour(colour(ColourRole::accent));
        g.fillPath(shapeOf(r.toFloat(), CornerSize::small));
        foreground = textColourOn(colour(ColourRole::accent));
    }

    if (!isActive)
        foreground = colour(ColourRole::textDisabled);

    g.setColour(foreground);
    g.setFont(getPopupMenuFont());

    r.reduce(8, 0);
    auto iconArea = r.removeFromLeft(14).toFloat();

    if (icon != nullptr)
    {
        icon->drawWithin(g, iconArea, juce::RectanglePlacement::centred, 1.0f);
    }
    else if (isTicked)
    {
        // A tick in the theme's accent, or the text colour on a highlight.
        juce::Path tick;
        const auto box = iconArea.withSizeKeepingCentre(10.0f, 10.0f);
        tick.startNewSubPath(box.getX(), box.getCentreY());
        tick.lineTo(box.getX() + 3.5f, box.getBottom() - 1.0f);
        tick.lineTo(box.getRight(), box.getY() + 1.0f);
        g.setColour(isHighlighted ? foreground : colour(ColourRole::accent));
        g.strokePath(tick, juce::PathStrokeType(2.0f));
        g.setColour(foreground);
    }

    r.removeFromLeft(6);

    if (hasSubMenu)
    {
        const auto arrowArea = r.removeFromRight(10).toFloat();
        juce::Path arrow;
        arrow.startNewSubPath(arrowArea.getX() + 2.0f, arrowArea.getCentreY() - 4.0f);
        arrow.lineTo(arrowArea.getRight() - 2.0f, arrowArea.getCentreY());
        arrow.lineTo(arrowArea.getX() + 2.0f, arrowArea.getCentreY() + 4.0f);
        g.strokePath(arrow, juce::PathStrokeType(1.5f));
    }

    g.drawFittedText(labelText(FontRole::label, text), r, juce::Justification::centredLeft, 1);

    if (shortcutKeyText.isNotEmpty())
    {
        g.setFont(font(FontRole::caption));
        g.setColour(isHighlighted && isActive ? foreground : colour(ColourRole::textSecondary));
        g.drawText(shortcutKeyText, r, juce::Justification::centredRight, true);
    }
}

juce::Font ThemedLookAndFeel::getPopupMenuFont()
{
    return font(FontRole::label);
}

void ThemedLookAndFeel::drawMenuBarBackground(juce::Graphics& g, int width, int height, bool,
                                              juce::MenuBarComponent&)
{
    g.fillAll(colour(ColourRole::panel));
    g.setColour(colour(ColourRole::separator));
    g.fillRect(0, height - 1, width, 1);
}

juce::Font ThemedLookAndFeel::getMenuBarFont(juce::MenuBarComponent&, int, const juce::String&)
{
    return font(FontRole::label);
}

//==============================================================================
void ThemedLookAndFeel::drawScrollbar(juce::Graphics& g, juce::ScrollBar& scrollbar, int x, int y,
                                      int width, int height, bool isScrollbarVertical,
                                      int thumbStartPosition, int thumbSize, bool isMouseOver,
                                      bool isMouseDown)
{
    if (thumbSize <= 0)
        return;

    const auto thumb = isScrollbarVertical
                           ? juce::Rectangle<int>(x, thumbStartPosition, width, thumbSize)
                           : juce::Rectangle<int>(thumbStartPosition, y, thumbSize, height);

    auto thumbColour = scrollbar.findColour(juce::ScrollBar::thumbColourId);
    if (isMouseOver || isMouseDown)
        thumbColour = colour(ColourRole::textSecondary);

    g.setColour(thumbColour);
    g.fillPath(shapeOf(thumb.toFloat().reduced(2.0f), CornerSize::small));
}

//==============================================================================
juce::Rectangle<int> ThemedLookAndFeel::getTooltipBounds(const juce::String& tipText,
                                                         juce::Point<int> screenPos,
                                                         juce::Rectangle<int> parentArea)
{
    const auto tipFont = font(FontRole::caption);
    juce::AttributedString text;
    text.append(labelText(FontRole::caption, tipText), tipFont);

    juce::TextLayout layout;
    layout.createLayout(text, 360.0f);

    const auto w = static_cast<int>(std::ceil(layout.getWidth())) + 16;
    const auto h = static_cast<int>(std::ceil(layout.getHeight())) + 10;

    return juce::Rectangle<int>(
               screenPos.x > parentArea.getCentreX() ? screenPos.x - (w + 12) : screenPos.x + 24,
               screenPos.y > parentArea.getCentreY() ? screenPos.y - (h + 6) : screenPos.y + 6, w,
               h)
        .constrainedWithin(parentArea);
}

void ThemedLookAndFeel::drawTooltip(juce::Graphics& g, const juce::String& text, int width,
                                    int height)
{
    const auto area = juce::Rectangle<int>(width, height).toFloat();
    const auto outline = shapeOf(area.reduced(theme.shape.border / 2.0f), CornerSize::small);

    g.setColour(colour(ColourRole::raised));
    g.fillPath(outline);
    g.setColour(colour(ColourRole::outline));
    g.strokePath(outline, juce::PathStrokeType(theme.shape.border));

    juce::AttributedString attributed;
    attributed.append(labelText(FontRole::caption, text), font(FontRole::caption),
                      colour(ColourRole::text));

    juce::TextLayout layout;
    layout.createLayout(attributed, static_cast<float>(width) - 16.0f);
    layout.draw(g, area.reduced(8.0f, 5.0f));
}

//==============================================================================
void ThemedLookAndFeel::fillTextEditorBackground(juce::Graphics& g, int width, int height,
                                                 juce::TextEditor& editor)
{
    g.setColour(editor.findColour(juce::TextEditor::backgroundColourId));
    g.fillPath(shapeOf(juce::Rectangle<int>(width, height).toFloat(), CornerSize::small));
}

void ThemedLookAndFeel::drawTextEditorOutline(juce::Graphics& g, int width, int height,
                                              juce::TextEditor& editor)
{
    if (!editor.isEnabled())
        return;

    const auto outline =
        shapeOf(juce::Rectangle<int>(width, height).toFloat().reduced(theme.shape.border / 2.0f),
                CornerSize::small);
    const bool focused = editor.hasKeyboardFocus(true) && !editor.isReadOnly();

    drawControlOutline(g, outline, focused,
                       editor.findColour(juce::TextEditor::focusedOutlineColourId));
}

//==============================================================================
void ThemedLookAndFeel::drawProgressBar(juce::Graphics& g, juce::ProgressBar& bar, int width,
                                        int height, double progress, const juce::String& textToShow)
{
    const auto area = juce::Rectangle<int>(width, height).toFloat();
    const auto outline = shapeOf(area.reduced(theme.shape.border / 2.0f), CornerSize::small);

    g.setColour(bar.findColour(juce::ProgressBar::backgroundColourId));
    g.fillPath(outline);

    if (progress >= 0.0 && progress <= 1.0)
    {
        const auto filled =
            area.reduced(3.0f).withWidth((area.getWidth() - 6.0f) * static_cast<float>(progress));
        g.setColour(bar.findColour(juce::ProgressBar::foregroundColourId));
        g.fillPath(shapeOf(filled, CornerSize::small));
    }
    else
    {
        // Unknown progress: a band sweeping across, driven by the bar's own repaints.
        const double phase = std::fmod(juce::Time::getMillisecondCounterHiRes() / 1200.0, 1.0);
        const float bandWidth = area.getWidth() * 0.25f;
        const float bandX = static_cast<float>(phase) * (area.getWidth() + bandWidth) - bandWidth;
        g.setColour(bar.findColour(juce::ProgressBar::foregroundColourId));
        g.reduceClipRegion(outline);
        g.fillRect(juce::Rectangle<float>(bandX, area.getY(), bandWidth, area.getHeight()));
    }

    drawControlOutline(g, outline, false, {});

    if (textToShow.isNotEmpty())
    {
        g.setColour(colour(ColourRole::text));
        g.setFont(font(FontRole::numeric,
                       std::min(theme.font(FontRole::numeric).size, area.getHeight() * 0.7f)));
        g.drawText(textToShow, area, juce::Justification::centred, false);
    }
}

//==============================================================================
void ThemedLookAndFeel::drawAlertBox(juce::Graphics& g, juce::AlertWindow& alert,
                                     const juce::Rectangle<int>& textArea, juce::TextLayout& layout)
{
    const auto area = alert.getLocalBounds().toFloat();
    const auto outline = shapeOf(area.reduced(theme.shape.border / 2.0f), CornerSize::large);

    g.setColour(alert.findColour(juce::AlertWindow::backgroundColourId));
    g.fillPath(outline);
    g.setColour(alert.findColour(juce::AlertWindow::outlineColourId));
    g.strokePath(outline, juce::PathStrokeType(theme.shape.border));

    // A stripe in the accent along the top marks the theme without an icon.
    g.setColour(colour(ColourRole::accent));
    g.fillRect(area.reduced(cornerSize(CornerSize::large) + 2.0f, 0.0f).withHeight(3.0f));

    g.setColour(alert.findColour(juce::AlertWindow::textColourId));
    layout.draw(g, textArea.toFloat());
}

juce::Font ThemedLookAndFeel::getAlertWindowTitleFont()
{
    return font(FontRole::heading);
}

juce::Font ThemedLookAndFeel::getAlertWindowMessageFont()
{
    return font(FontRole::label);
}

juce::Font ThemedLookAndFeel::getAlertWindowFont()
{
    return font(FontRole::label);
}

//==============================================================================
void ThemedLookAndFeel::drawGroupComponentOutline(juce::Graphics& g, int width, int height,
                                                  const juce::String& text,
                                                  const juce::Justification&,
                                                  juce::GroupComponent& group)
{
    const auto headingFont = font(FontRole::heading);
    const float textHeight = headingFont.getHeight();
    const auto area = juce::Rectangle<int>(width, height)
                          .toFloat()
                          .withTrimmedTop(textHeight / 2.0f)
                          .reduced(theme.shape.border / 2.0f);

    g.setColour(group.findColour(juce::GroupComponent::outlineColourId));
    g.strokePath(shapeOf(area, CornerSize::medium), juce::PathStrokeType(theme.shape.border));

    if (text.isNotEmpty())
    {
        const auto shown = labelText(appearance::FontRole::heading, text);
        const float textWidth = juce::GlyphArrangement::getStringWidth(headingFont, shown) + 12.0f;
        const juce::Rectangle<float> textBox(10.0f, 0.0f, textWidth, textHeight);

        g.setColour(colour(ColourRole::panel));
        g.fillRect(textBox);
        g.setColour(group.findColour(juce::GroupComponent::textColourId));
        g.setFont(headingFont);
        g.drawText(shown, textBox, juce::Justification::centred, false);
    }
}

void ThemedLookAndFeel::drawCallOutBoxBackground(juce::CallOutBox&, juce::Graphics& g,
                                                 const juce::Path& path, juce::Image&)
{
    g.setColour(colour(ColourRole::raised));
    g.fillPath(path);
    g.setColour(colour(ColourRole::outline));
    g.strokePath(path, juce::PathStrokeType(theme.shape.border));
}

//==============================================================================
// The DAW's own components.

void ThemedLookAndFeel::drawPanel(juce::Graphics& g, juce::Rectangle<float> area,
                                  ColourRole surface, bool separatorBelow) const
{
    g.setColour(colour(surface));
    g.fillRect(area);

    if (separatorBelow)
    {
        g.setColour(colour(ColourRole::separator));
        g.fillRect(area.withTop(area.getBottom() - 1.0f));
    }
}

void ThemedLookAndFeel::drawLane(juce::Graphics& g, juce::Rectangle<float> area, int row,
                                 bool isTrackRow, bool translucent) const
{
    const auto fill = colour(!isTrackRow    ? ColourRole::panelAlt
                             : row % 2 == 0 ? ColourRole::lane
                                            : ColourRole::laneAlt);
    g.setColour(translucent ? fill.withAlpha(0.72f) : fill);
    g.fillRect(area);
}

void ThemedLookAndFeel::drawGridLine(juce::Graphics& g, float x, float top, float bottom,
                                     bool barLine) const
{
    g.setColour(colour(barLine ? ColourRole::gridBar : ColourRole::gridBeat));
    g.drawVerticalLine(juce::roundToInt(x), top, bottom);
}

void ThemedLookAndFeel::drawLoopShade(juce::Graphics& g, juce::Rectangle<float> area,
                                      bool enabled) const
{
    g.setColour(colour(ColourRole::loopShade).withMultipliedAlpha(enabled ? 1.0f : 0.45f));
    g.fillRect(area);
}

int ThemedLookAndFeel::playheadReach() const noexcept
{
    return 3 + static_cast<int>(std::ceil(theme.shape.glow)) + playheadTrail();
}

bool ThemedLookAndFeel::playheadAnimates(bool) const
{
    return false;
}

void ThemedLookAndFeel::drawTimelineBackground(juce::Graphics&, juce::Rectangle<float>,
                                               double) const
{
}

void ThemedLookAndFeel::drawHazardBand(juce::Graphics&, juce::Rectangle<float>, double, bool) const
{
}

float ThemedLookAndFeel::pulseAt(double seconds) noexcept
{
    return static_cast<float>(0.5 -
                              0.5 * std::cos(seconds * juce::MathConstants<double>::twoPi * 0.9));
}

void ThemedLookAndFeel::drawRecordPulse(juce::Graphics& g, juce::Rectangle<float> area,
                                        juce::Colour fill, float pulse) const
{
    // A ring in the colour that reads on the fill, so the label stays as readable.
    g.setColour(textColourOn(fill).withAlpha(0.75f * pulse));
    g.strokePath(shapeOf(area.reduced(theme.shape.border + 1.5f), CornerSize::small),
                 juce::PathStrokeType(1.5f));
}

void ThemedLookAndFeel::drawPlayhead(juce::Graphics& g, float x, float top, float bottom,
                                     const PlayheadLook&) const
{
    const auto line = juce::Rectangle<float>(x - 0.75f, top, 1.5f, bottom - top);
    juce::Path shape;
    shape.addRectangle(line);
    drawGlow(g, shape, colour(ColourRole::playhead));

    g.setColour(colour(ColourRole::playhead));
    g.fillRect(line);
}

void ThemedLookAndFeel::drawDropIndicator(juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour(colour(ColourRole::focus));
    g.strokePath(shapeOf(area, CornerSize::medium), juce::PathStrokeType(2.0f));
}

ThemedLookAndFeel::ClipStyle ThemedLookAndFeel::clipStyle(int trackIndex, ClipState state,
                                                          int row) const
{
    const auto lane = theme.colour(row % 2 == 0 ? ColourRole::lane : ColourRole::laneAlt)
                          .over(theme.colour(ColourRole::window));
    const auto base = (state == ClipState::failed      ? theme.colour(ColourRole::error)
                       : state == ClipState::recording ? theme.colour(ColourRole::record)
                                                       : theme.clips.colourFor(trackIndex))
                          .withAlpha(1.0f);

    // A clip still loading shows only faintly; its name stays readable.
    const float bodyAlpha = theme.clips.bodyAlpha * (state == ClipState::loading ? 0.5f : 1.0f);
    const auto header = base.withAlpha(theme.clips.headerAlpha).over(lane);

    ClipStyle style;
    style.body = toJuce(base.withAlpha(bodyAlpha).over(lane));
    style.header = toJuce(header);
    style.name = colour(appearance::textRoleOn(theme, header));
    style.waveform = toJuce(base);
    style.outline = toJuce(base);
    return style;
}

float ThemedLookAndFeel::clipHeaderHeight() const
{
    return std::max(15.0f, std::ceil(font(FontRole::caption).getHeight()) + 3.0f);
}

void ThemedLookAndFeel::drawClipFrame(juce::Graphics& g, juce::Rectangle<float> bounds,
                                      const ClipStyle& style) const
{
    const auto outline = shapeOf(bounds, CornerSize::medium);
    g.setColour(style.body);
    g.fillPath(outline);

    juce::Graphics::ScopedSaveState saved(g);
    g.reduceClipRegion(outline);
    g.setColour(style.header);
    g.fillRect(bounds.withHeight(clipHeaderHeight()));
}

void ThemedLookAndFeel::drawClipOutline(juce::Graphics& g, juce::Rectangle<float> bounds,
                                        const ClipStyle& style, bool selected) const
{
    const auto outline = shapeOf(bounds.reduced(0.5f), CornerSize::medium);

    if (selected)
    {
        drawGlow(g, outline, colour(ColourRole::selection));
        g.setColour(colour(ColourRole::selection));
        g.strokePath(outline, juce::PathStrokeType(2.0f));
        return;
    }

    g.setColour(style.outline.withMultipliedAlpha(0.9f));
    g.strokePath(outline, juce::PathStrokeType(theme.shape.border));
}

void ThemedLookAndFeel::drawClipName(juce::Graphics& g, juce::Rectangle<float> area,
                                     const juce::String& name, const ClipStyle& style) const
{
    g.setColour(style.name);
    g.setFont(font(FontRole::caption));
    g.drawText(labelText(FontRole::caption, name), area, juce::Justification::centredLeft, true);
}

void ThemedLookAndFeel::drawRulerBackground(juce::Graphics& g, juce::Rectangle<float> area) const
{
    drawPanel(g, area, ColourRole::panelAlt, true);
}

void ThemedLookAndFeel::drawRulerMark(juce::Graphics& g, float x, float top, float bottom,
                                      bool major) const
{
    g.setColour(colour(major ? ColourRole::textSecondary : ColourRole::outline));
    g.drawVerticalLine(juce::roundToInt(x), top, bottom);
}

void ThemedLookAndFeel::drawRulerLabel(juce::Graphics& g, const juce::String& text,
                                       juce::Rectangle<float> area, bool major) const
{
    g.setColour(colour(major ? ColourRole::text : ColourRole::textSecondary));
    g.setFont(font(major ? FontRole::numeric : FontRole::caption,
                   std::min(theme.font(FontRole::caption).size, area.getHeight())));
    g.drawText(text, area, juce::Justification::centredLeft, false);
}

void ThemedLookAndFeel::drawRulerLoop(juce::Graphics& g, float x0, float x1, float height,
                                      bool enabled) const
{
    const auto edge = colour(enabled ? ColourRole::loop : ColourRole::textDisabled);
    g.setColour(edge.withAlpha(enabled ? 0.3f : 0.18f));
    g.fillRect(juce::Rectangle<float>(x0, 0.0f, x1 - x0, height));
    g.setColour(edge);
    g.fillRect(juce::Rectangle<float>(x0, 0.0f, 2.0f, height));
    g.fillRect(juce::Rectangle<float>(x1 - 2.0f, 0.0f, 2.0f, height));
    g.fillRect(juce::Rectangle<float>(x0, 0.0f, x1 - x0, 3.0f));
}

void ThemedLookAndFeel::drawPlayheadMarker(juce::Graphics& g, float x, float bottom) const
{
    juce::Path marker;
    marker.addTriangle(x - 6.0f, bottom - 10.0f, x + 6.0f, bottom - 10.0f, x, bottom);
    g.setColour(colour(ColourRole::playhead));
    g.fillPath(marker);
}

void ThemedLookAndFeel::drawMeterBar(juce::Graphics& g, juce::Rectangle<float> bar, float levelDb,
                                     float peakHoldDb, const MeterAnimation& animation) const
{
    using engine::dsp::MeterBallistics;
    const auto yFor = [&bar](float decibels)
    {
        return bar.getBottom() - MeterBallistics::toProportion(decibels) * bar.getHeight();
    };

    g.setColour(colour(ColourRole::meterTrack));
    g.fillRect(bar);

    const auto& meter = theme.meter;

    if (meter.style == appearance::MeterStyle::segments)
    {
        // LEDs from the bottom up; each lit in its zone's colour, unlit ones faintly.
        const float step = meter.segmentHeight + meter.segmentGap;
        for (float top = bar.getBottom() - meter.segmentHeight; top >= bar.getY() - 0.5f;
             top -= step)
        {
            const auto segment =
                juce::Rectangle<float>(bar.getX(), top, bar.getWidth(), meter.segmentHeight);
            const float proportion = (bar.getBottom() - segment.getCentreY()) / bar.getHeight();
            const float decibels = MeterBallistics::floorDb * (1.0f - proportion);
            const auto lit = toJuce(meter.colourAt(decibels));
            const bool on =
                levelDb > MeterBallistics::floorDb && segment.getCentreY() >= yFor(levelDb);
            // Animated: LEDs that were just lit fade out instead of going dark at once.
            const bool fading = !on && animation.animated &&
                                animation.afterglowDb > MeterBallistics::floorDb &&
                                segment.getCentreY() >= yFor(animation.afterglowDb);
            g.setColour(on ? lit : lit.withAlpha(fading ? 0.45f : 0.12f));
            g.fillRect(segment);
        }
    }
    else if (levelDb > MeterBallistics::floorDb)
    {
        juce::ColourGradient gradient(toJuce(meter.colourAt(MeterBallistics::floorDb)), 0.0f,
                                      bar.getBottom(), toJuce(meter.colourAt(0.0f)), 0.0f,
                                      bar.getY(), false);
        for (const auto& stop : meter.stops)
            gradient.addColour(MeterBallistics::toProportion(stop.decibels), toJuce(stop.colour));

        g.setGradientFill(gradient);
        g.fillRect(bar.withTop(yFor(levelDb)));

        // Animated: the top of the level glows in its colour.
        if (animation.animated && theme.shape.glow > 0.0f)
        {
            juce::Path top;
            top.addRectangle(bar.withTop(yFor(levelDb)).withHeight(2.0f));
            drawGlow(g, top, toJuce(meter.colourAt(levelDb)), 0.8f);
        }
    }

    if (peakHoldDb > MeterBallistics::floorDb)
    {
        g.setColour(colour(ColourRole::peakHold));
        g.fillRect(bar.withTop(yFor(peakHoldDb)).withHeight(2.0f));
    }
}

void ThemedLookAndFeel::drawClipIndicator(juce::Graphics& g, juce::Rectangle<float> area,
                                          bool clipping) const
{
    g.setColour(colour(clipping ? ColourRole::clip : ColourRole::meterTrack));
    g.fillPath(shapeOf(area, CornerSize::small));
}

void ThemedLookAndFeel::drawMeterScale(juce::Graphics& g, juce::Rectangle<float> area,
                                       std::span<const float> marksDb) const
{
    using engine::dsp::MeterBallistics;
    const auto scaleFont = font(FontRole::caption, theme.font(FontRole::caption).size * 0.85f);
    g.setFont(scaleFont);
    g.setColour(colour(ColourRole::textSecondary));

    for (const float decibels : marksDb)
    {
        const float y =
            area.getBottom() - MeterBallistics::toProportion(decibels) * area.getHeight();
        const auto text = decibels == 0.0f ? juce::String("0") : juce::String(decibels, 0);
        g.drawText(text, juce::Rectangle<float>(area.getX(), y - 6.0f, area.getWidth(), 12.0f),
                   juce::Justification::centredRight, false);
    }
}

void ThemedLookAndFeel::drawTrackHeader(juce::Graphics& g, juce::Rectangle<float> area) const
{
    drawPanel(g, area, ColourRole::panel, true);
}

void ThemedLookAndFeel::drawLogo(juce::Graphics& g, juce::Rectangle<float> area,
                                 const juce::String& text, double, bool) const
{
    g.setColour(colour(ColourRole::accent));
    g.setFont(font(FontRole::title, std::min(26.0f, area.getHeight() * 0.62f)));
    g.drawText(labelText(FontRole::title, text), area, juce::Justification::centred, false);
}

void ThemedLookAndFeel::drawStatusBar(juce::Graphics& g, juce::Rectangle<float> area,
                                      const StatusLine& line) const
{
    const bool prompt = theme.shape.statusBar == appearance::StatusBarStyle::prompt;
    const auto surface = prompt ? ColourRole::inset : ColourRole::panel;

    g.setColour(colour(surface));
    g.fillRect(area);
    g.setColour(colour(ColourRole::separator));
    g.fillRect(area.withHeight(1.0f));

    auto text = area.reduced(10.0f, 0.0f);
    const auto textFont = font(prompt ? FontRole::label : FontRole::caption);
    g.setFont(textFont);

    if (line.details.isNotEmpty())
    {
        const float width = juce::GlyphArrangement::getStringWidth(textFont, line.details);
        g.setColour(colour(ColourRole::textSecondary));
        g.drawText(line.details, text.removeFromRight(width + 4.0f),
                   juce::Justification::centredRight, false);
    }

    if (prompt)
    {
        // A shell prompt: "daw:project$ message", then the cursor.
        const auto promptText = "daw:" + line.projectName.replaceCharacter(' ', '_') + "$ ";
        const float promptWidth = juce::GlyphArrangement::getStringWidth(textFont, promptText);
        g.setColour(colour(ColourRole::accent));
        g.drawText(promptText, text.removeFromLeft(promptWidth), juce::Justification::centredLeft,
                   false);
    }

    const float messageWidth =
        std::min(text.getWidth(), juce::GlyphArrangement::getStringWidth(textFont, line.message));
    g.setColour(colour(line.messageRole));
    g.drawText(line.message, text.removeFromLeft(messageWidth), juce::Justification::centredLeft,
               true);

    if (prompt && line.cursorVisible)
    {
        const float height = textFont.getHeight() * 0.85f;
        g.setColour(colour(ColourRole::accent));
        g.fillRect(juce::Rectangle<float>(text.getX() + 1.0f, area.getCentreY() - height / 2.0f,
                                          height * 0.5f, height));
    }
}

void ThemedLookAndFeel::drawMixerStrip(juce::Graphics& g, juce::Rectangle<float> area,
                                       StripKind kind) const
{
    g.setColour(colour(kind == StripKind::track ? ColourRole::panel
                       : kind == StripKind::bus ? ColourRole::panelAlt
                                                : ColourRole::raised));
    g.fillRect(area);
    g.setColour(colour(ColourRole::separator));
    g.fillRect(area.withLeft(area.getRight() - 1.0f));
}

void ThemedLookAndFeel::drawSlotList(juce::Graphics& g, juce::Rectangle<float> area) const
{
    g.setColour(colour(ColourRole::inset));
    g.fillPath(shapeOf(area, CornerSize::small));
}

void ThemedLookAndFeel::drawEffectSlot(juce::Graphics& g, juce::Rectangle<float> area,
                                       const juce::String& name, bool bypassed, bool failed) const
{
    const auto fill = failed ? colour(ColourRole::error) : colour(ColourRole::raised);
    g.setColour(fill);
    g.fillPath(shapeOf(area, CornerSize::small));

    g.setColour(failed     ? textColourOn(fill)
                : bypassed ? colour(ColourRole::textDisabled)
                           : colour(ColourRole::text));
    g.setFont(font(FontRole::caption));
    g.drawText(labelText(FontRole::caption, (failed ? "! " : "") + name), area.reduced(4.0f, 0.0f),
               juce::Justification::centredLeft, true);
}

void ThemedLookAndFeel::drawSendSlot(juce::Graphics& g, juce::Rectangle<float> area,
                                     const juce::String& name, float levelProportion,
                                     bool automated) const
{
    const auto shape = shapeOf(area, CornerSize::small);
    g.setColour(colour(ColourRole::raised));
    g.fillPath(shape);

    // The level: a faint band behind the name and a line along the bottom.
    const auto level = colour(automated ? ColourRole::automation : ColourRole::accentAlt);
    const auto filled = area.withWidth(area.getWidth() * std::clamp(levelProportion, 0.0f, 1.0f));
    {
        juce::Graphics::ScopedSaveState saved(g);
        g.reduceClipRegion(shape);
        g.setColour(level.withAlpha(0.22f));
        g.fillRect(filled);
        g.setColour(level);
        g.fillRect(filled.withTop(filled.getBottom() - 2.0f));
    }

    g.setColour(colour(ColourRole::text));
    g.setFont(font(FontRole::caption));
    g.drawText(labelText(FontRole::caption, name), area.reduced(4.0f, 0.0f),
               juce::Justification::centredLeft, true);
}

void ThemedLookAndFeel::drawEmptySlot(juce::Graphics& g, juce::Rectangle<float> area,
                                      const juce::String& text) const
{
    g.setColour(colour(ColourRole::textSecondary));
    g.setFont(font(FontRole::caption));
    g.drawText(labelText(FontRole::caption, text), area.reduced(4.0f, 0.0f),
               juce::Justification::centredLeft, true);
}

void ThemedLookAndFeel::drawGraphBackground(juce::Graphics& g, juce::Rectangle<float> area) const
{
    const auto shape = shapeOf(area, CornerSize::small);
    g.setColour(colour(ColourRole::inset));
    g.fillPath(shape);
    g.setColour(colour(ColourRole::separator));
    g.strokePath(shape, juce::PathStrokeType(1.0f));
}

void ThemedLookAndFeel::drawGraphLine(juce::Graphics& g, juce::Line<float> line,
                                      bool emphasised) const
{
    g.setColour(colour(emphasised ? ColourRole::gridBar : ColourRole::gridBeat));
    g.drawLine(line, 1.0f);
}

void ThemedLookAndFeel::drawGraphLabel(juce::Graphics& g, const juce::String& text,
                                       juce::Rectangle<float> area,
                                       juce::Justification justification) const
{
    g.setColour(colour(ColourRole::textSecondary));
    g.setFont(font(FontRole::caption, theme.font(FontRole::caption).size * 0.9f));
    g.drawText(text, area, justification, false);
}

void ThemedLookAndFeel::drawCurve(juce::Graphics& g, const juce::Path& curve, float baselineY,
                                  bool filled) const
{
    if (curve.isEmpty())
        return;

    if (filled)
    {
        const auto bounds = curve.getBounds();
        auto area = curve;
        area.lineTo(bounds.getRight(), baselineY);
        area.lineTo(bounds.getX(), baselineY);
        area.closeSubPath();
        g.setColour(colour(ColourRole::curveFill));
        g.fillPath(area);
    }

    g.setColour(colour(ColourRole::curve));
    g.strokePath(curve, juce::PathStrokeType(2.0f));
}

juce::Colour ThemedLookAndFeel::seriesColour(int index) const
{
    return toJuce(theme.clips.colourFor(index).withAlpha(1.0f));
}

void ThemedLookAndFeel::drawSeriesCurve(juce::Graphics& g, const juce::Path& curve,
                                        int series) const
{
    g.setColour(seriesColour(series).withAlpha(0.5f));
    g.strokePath(curve, juce::PathStrokeType(1.5f));
}

void ThemedLookAndFeel::drawCurveHandle(juce::Graphics& g, juce::Point<float> centre, int series,
                                        bool on, bool selected, const juce::String& label) const
{
    const auto fill = seriesColour(series);
    const auto disc = juce::Rectangle<float>(14.0f, 14.0f).withCentre(centre);

    if (selected)
    {
        juce::Path ring;
        ring.addEllipse(disc.expanded(2.5f));
        drawGlow(g, ring, fill);
        g.setColour(colour(ColourRole::selection));
        g.strokePath(ring, juce::PathStrokeType(1.5f));
    }

    g.setColour(on ? fill : fill.withAlpha(0.3f));
    g.fillEllipse(disc);
    g.setColour(on ? textColourOn(fill) : colour(ColourRole::text));
    g.setFont(font(FontRole::caption, 11.0f));
    g.drawText(label, disc, juce::Justification::centred, false);
}

void ThemedLookAndFeel::drawLevelDot(juce::Graphics& g, juce::Point<float> centre) const
{
    g.setColour(colour(ColourRole::highlight));
    g.fillEllipse(juce::Rectangle<float>(8.0f, 8.0f).withCentre(centre));
}

void ThemedLookAndFeel::drawGainReductionBar(juce::Graphics& g, juce::Rectangle<float> bar,
                                             float proportion) const
{
    g.setColour(colour(ColourRole::meterTrack));
    g.fillRect(bar);
    g.setColour(colour(ColourRole::gainReduction));
    g.fillRect(bar.withHeight(bar.getHeight() * std::clamp(proportion, 0.0f, 1.0f)));
}

void ThemedLookAndFeel::drawGainReductionTrace(juce::Graphics& g, const juce::Path& trace) const
{
    g.setColour(colour(ColourRole::gainReduction));
    g.strokePath(trace, juce::PathStrokeType(1.5f));
}

void ThemedLookAndFeel::drawAutomationShade(juce::Graphics& g, juce::Rectangle<float> row) const
{
    // Dims the clips under the lane so the curve reads first.
    g.setColour(colour(ColourRole::shadow).withMultipliedAlpha(0.4f));
    g.fillRect(row);
}

void ThemedLookAndFeel::drawAutomationCurve(juce::Graphics& g, const juce::Path& curve,
                                            bool reads) const
{
    g.setColour(colour(reads ? ColourRole::automation : ColourRole::textSecondary));
    g.strokePath(curve, juce::PathStrokeType(1.8f));
}

void ThemedLookAndFeel::drawAutomationLevel(juce::Graphics& g, const juce::Path& line,
                                            bool reads) const
{
    juce::Path dashed;
    const float dashes[] = {4.0f, 4.0f};
    juce::PathStrokeType(1.0f).createDashedStroke(dashed, line, dashes, 2);
    g.setColour(colour(reads ? ColourRole::automation : ColourRole::textSecondary));
    g.fillPath(dashed);
}

void ThemedLookAndFeel::drawAutomationPoint(juce::Graphics& g, juce::Point<float> centre,
                                            float radius, bool reads) const
{
    const auto disc = juce::Rectangle<float>(2.0f * radius, 2.0f * radius).withCentre(centre);
    g.setColour(colour(reads ? ColourRole::automation : ColourRole::textSecondary));
    g.fillEllipse(disc);
    g.setColour(colour(ColourRole::text));
    g.drawEllipse(disc, 1.0f);
}

void ThemedLookAndFeel::drawAutomationLabel(juce::Graphics& g, const juce::String& text,
                                            juce::Rectangle<float> area, bool emphasised) const
{
    g.setColour(colour(emphasised ? ColourRole::text : ColourRole::textSecondary));
    g.setFont(font(FontRole::caption));
    g.drawText(text, area, juce::Justification::topLeft, false);
}

} // namespace daw::ui::desktop
