#include "daw/appearance/ThemeLibrary.h"
#include "daw/ui/desktop/ThemeFonts.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdlib>

using namespace daw;
using appearance::ColourRole;
using ui::desktop::CornerSize;
using ui::desktop::ThemedLookAndFeel;
using ui::desktop::toJuce;

namespace
{
bool isClose(juce::Colour actual, juce::Colour expected, int tolerance = 2)
{
    return std::abs(actual.getRed() - expected.getRed()) <= tolerance &&
           std::abs(actual.getGreen() - expected.getGreen()) <= tolerance &&
           std::abs(actual.getBlue() - expected.getBlue()) <= tolerance;
}

juce::Image paint(juce::Component& component, ThemedLookAndFeel& lookAndFeel, int width, int height)
{
    component.setLookAndFeel(&lookAndFeel);
    component.setBounds(0, 0, width, height);
    auto image = component.createComponentSnapshot(component.getLocalBounds(), true, 1.0f);
    component.setLookAndFeel(nullptr);
    return image;
}
} // namespace

TEST_CASE("Every theme's fonts load from the bundled typefaces", "[ui]")
{
    const ui::desktop::ThemeFonts fonts;
    for (const auto& theme : appearance::ThemeLibrary::builtIn().getThemes())
    {
        for (const auto& spec : theme.fonts)
        {
            INFO(theme.name << ": " << spec.family << " " << spec.style);
            CHECK(fonts.isLoaded(spec));
            CHECK(fonts.font(spec).getHeight() == spec.size);
        }
    }

    appearance::FontSpec unknown{"No Such Family", "Bold", 14.0f, 0.0f, false};
    CHECK_FALSE(fonts.isLoaded(unknown));
    CHECK(fonts.font(unknown).getHeight() == 14.0f);
}

TEST_CASE("Every theme sets JUCE's widget colours from its colour roles", "[ui]")
{
    const ui::desktop::ThemeFonts fonts;
    for (const auto& theme : appearance::ThemeLibrary::builtIn().getThemes())
    {
        INFO(theme.name);
        const auto lookAndFeel = ThemedLookAndFeel::make(theme, fonts);
        const auto role = [&theme](ColourRole r)
        {
            return toJuce(theme.colour(r));
        };

        CHECK(lookAndFeel->findColour(juce::ResizableWindow::backgroundColourId) ==
              role(ColourRole::window));
        CHECK(lookAndFeel->findColour(juce::TextButton::buttonColourId) ==
              role(ColourRole::raised));
        CHECK(lookAndFeel->findColour(juce::TextButton::buttonOnColourId) ==
              role(ColourRole::accent));
        CHECK(lookAndFeel->findColour(juce::Label::textColourId) == role(ColourRole::text));
        CHECK(lookAndFeel->findColour(juce::PopupMenu::backgroundColourId) ==
              role(ColourRole::raised));
        CHECK(lookAndFeel->findColour(juce::TextEditor::backgroundColourId) ==
              role(ColourRole::inset));
        CHECK(lookAndFeel->findColour(juce::AlertWindow::backgroundColourId) ==
              role(ColourRole::panel));
        CHECK(lookAndFeel->findColour(juce::TooltipWindow::textColourId) == role(ColourRole::text));
        CHECK(lookAndFeel->findColour(juce::TextButton::textColourOnId) ==
              lookAndFeel->textColourOn(role(ColourRole::accent)));
    }
}

TEST_CASE("Every theme paints JUCE's widgets in its own colours", "[ui]")
{
    const ui::desktop::ThemeFonts fonts;
    for (const auto& theme : appearance::ThemeLibrary::builtIn().getThemes())
    {
        INFO(theme.name);
        const auto lookAndFeel = ThemedLookAndFeel::make(theme, fonts);

        juce::TextButton button("Export");
        const auto buttonImage = paint(button, *lookAndFeel, 120, 30);
        CHECK(isClose(buttonImage.getPixelAt(8, 15), toJuce(theme.colour(ColourRole::raised))));

        button.setToggleState(true, juce::dontSendNotification);
        const auto onImage = paint(button, *lookAndFeel, 120, 30);
        CHECK(isClose(onImage.getPixelAt(8, 15), toJuce(theme.colour(ColourRole::accent)), 40));

        // Everything else at least paints without trouble in every theme.
        juce::ToggleButton toggle("Follow the system");
        toggle.setToggleState(true, juce::dontSendNotification);
        paint(toggle, *lookAndFeel, 160, 24);

        juce::Slider fader(juce::Slider::LinearVertical, juce::Slider::TextBoxBelow);
        fader.setRange(-60.0, 6.0);
        fader.setValue(-6.0);
        paint(fader, *lookAndFeel, 40, 160);

        juce::Slider pan(juce::Slider::RotaryHorizontalVerticalDrag, juce::Slider::NoTextBox);
        pan.setRange(-1.0, 1.0);
        pan.setValue(-0.4);
        paint(pan, *lookAndFeel, 40, 40);

        juce::ComboBox box;
        box.addItem("24-bit", 1);
        box.setSelectedId(1);
        paint(box, *lookAndFeel, 120, 26);

        juce::TextEditor editor;
        editor.setText("120.00");
        paint(editor, *lookAndFeel, 120, 26);

        double progress = 0.42;
        juce::ProgressBar bar(progress);
        paint(bar, *lookAndFeel, 200, 20);

        juce::Label label({}, "-12.0 dB");
        label.getProperties().set(ThemedLookAndFeel::fontRoleProperty, "numeric");
        CHECK(lookAndFeel->getLabelFont(label).getHeight() ==
              theme.font(appearance::FontRole::numeric).size);
        paint(label, *lookAndFeel, 80, 20);
    }
}

TEST_CASE("Shapes follow the theme's corners: rounded, sharp or cut", "[ui]")
{
    const ui::desktop::ThemeFonts fonts;
    const auto& library = appearance::ThemeLibrary::builtIn();
    const juce::Rectangle<float> area(0.0f, 0.0f, 100.0f, 40.0f);
    const juce::Point<float> nearTopLeft(0.5f, 0.5f);
    const juce::Point<float> nearTopRight(99.5f, 0.5f);

    const auto neon = ThemedLookAndFeel::make(*library.find("neon-grid"), fonts);
    const auto rounded = neon->shapeOf(area, CornerSize::large);
    CHECK(rounded.contains(area.getCentre()));
    CHECK_FALSE(rounded.contains(nearTopLeft));
    CHECK_FALSE(rounded.contains(nearTopRight));

    const auto acid = ThemedLookAndFeel::make(*library.find("acid-terminal"), fonts);
    const auto sharp = acid->shapeOf(area, CornerSize::large);
    CHECK(sharp.contains(nearTopLeft));
    CHECK(sharp.contains(nearTopRight));

    const auto synth = ThemedLookAndFeel::make(*library.find("synth-chrome"), fonts);
    const auto cut = synth->shapeOf(area, CornerSize::large);
    CHECK_FALSE(cut.contains(nearTopLeft));
    CHECK(cut.contains(nearTopRight));
    CHECK(cut.getBounds() == area);
}

TEST_CASE("Text on a fill takes the theme's text colour that reads on it", "[ui]")
{
    const ui::desktop::ThemeFonts fonts;
    const auto& theme = *appearance::ThemeLibrary::builtIn().find("acid-terminal");
    const auto lookAndFeel = ThemedLookAndFeel::make(theme, fonts);

    CHECK(lookAndFeel->textColourOn(toJuce(theme.colour(ColourRole::accent))) ==
          toJuce(theme.colour(ColourRole::textOnAccent)));
    CHECK(lookAndFeel->textColourOn(toJuce(theme.colour(ColourRole::automation))) ==
          toJuce(theme.colour(ColourRole::text)));
}
