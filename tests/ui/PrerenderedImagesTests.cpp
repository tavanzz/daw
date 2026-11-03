#include "daw/appearance/ThemeLibrary.h"
#include "daw/ui/desktop/PrerenderedImages.h"
#include "daw/ui/desktop/ThemeManager.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <catch2/catch_test_macros.hpp>

#include <tuple>

using namespace daw;
using ui::desktop::PrerenderedImages;

namespace
{
void fill(juce::Graphics& g, juce::Rectangle<int>)
{
    g.fillAll(juce::Colours::white);
}

void paintToggledButton(juce::LookAndFeel& lookAndFeel)
{
    juce::TextButton button("Solo");
    button.setToggleState(true, juce::dontSendNotification);
    button.setLookAndFeel(&lookAndFeel);
    button.setBounds(0, 0, 60, 24);
    juce::Image image(juce::Image::ARGB, 60, 24, true);
    juce::Graphics g(image);
    button.paintEntireComponent(g, false);
    button.setLookAndFeel(nullptr);
}
} // namespace

TEST_CASE("Prerendered images are painted once per key and size", "[ui]")
{
    PrerenderedImages images;

    const auto first = images.get("glow", {40, 20}, fill);
    const auto again = images.get("glow", {40, 20}, fill);
    CHECK(images.getPaintCount() == 1);
    CHECK(first.getWidth() == 40);
    CHECK(again.getPixelAt(5, 5) == juce::Colours::white);

    const auto resized = images.get("glow", {80, 20}, fill);
    CHECK(images.getPaintCount() == 2);
    CHECK(resized.getWidth() == 80);

    std::ignore = images.get("stripes", {40, 20}, fill);
    CHECK(images.getPaintCount() == 3);
    CHECK(images.size() == 3);

    images.clear();
    std::ignore = images.get("glow", {40, 20}, fill);
    CHECK(images.getPaintCount() == 4);
}

TEST_CASE("The least recently used prerendered image goes first when the set is full", "[ui]")
{
    PrerenderedImages images;

    for (std::size_t index = 0; index < PrerenderedImages::capacity; ++index)
        std::ignore = images.get("image " + juce::String(static_cast<int>(index)), {4, 4}, fill);

    std::ignore = images.get("image 0", {4, 4}, fill); // used again: now the most recent
    std::ignore = images.get("one more", {4, 4}, fill);
    CHECK(images.size() == PrerenderedImages::capacity);

    const int paints = images.getPaintCount();
    std::ignore = images.get("image 0", {4, 4}, fill);
    CHECK(images.getPaintCount() == paints); // still there
    std::ignore = images.get("image 1", {4, 4}, fill);
    CHECK(images.getPaintCount() == paints + 1); // was the oldest, went
}

TEST_CASE("Glows are painted once and reused, only in themes that have them", "[ui]")
{
    const ui::desktop::ThemeFonts fonts;
    const auto& library = appearance::ThemeLibrary::builtIn();

    const auto neon = ui::desktop::ThemedLookAndFeel::make(*library.find("neon-grid"), fonts);
    paintToggledButton(*neon);
    paintToggledButton(*neon);
    CHECK(neon->getImages().getPaintCount() == 1);

    const auto acid = ui::desktop::ThemedLookAndFeel::make(*library.find("acid-terminal"), fonts);
    paintToggledButton(*acid);
    CHECK(acid->getImages().getPaintCount() == 0);
}

TEST_CASE("The theme that leaves keeps no prerendered images", "[ui]")
{
    ui::desktop::ThemeManager themes({});
    auto& neon = themes.getLookAndFeel();
    paintToggledButton(neon);
    REQUIRE(neon.getImages().size() == 1);

    themes.setTheme("acid-terminal");
    CHECK(neon.getImages().size() == 0);
}
