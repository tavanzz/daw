#include "daw/appearance/ThemeFile.h"
#include "daw/appearance/ThemeLibrary.h"

#include <catch2/catch_test_macros.hpp>

using namespace daw::appearance;

namespace
{
const Theme& builtInTheme(const char* id)
{
    const auto* theme = ThemeLibrary::builtIn().find(id);
    REQUIRE(theme != nullptr);
    return *theme;
}

std::string hexOf(const Theme& theme, ColourRole role)
{
    return theme.colour(role).toHex();
}
} // namespace

TEST_CASE("Every built-in theme defines every value", "[appearance]")
{
    const auto files = builtInThemeFiles();
    REQUIRE(files.size() == 3);

    for (const auto& file : files)
    {
        INFO(file.fileName.toStdString());
        REQUIRE(file.json.isNotEmpty());

        const auto result = readTheme(file.json);
        INFO(result.errors.joinIntoString("\n").toStdString());
        CHECK(result.errors.isEmpty());
        REQUIRE(result.theme.has_value());

        for (const auto& font : result.theme->fonts)
        {
            CHECK(font.family.isNotEmpty());
            CHECK(font.style.isNotEmpty());
            CHECK(font.size > 0.0f);
        }

        CHECK(result.theme->clips.colours.size() >= 4);
        CHECK(result.theme->meter.stops.size() >= 2);
        CHECK(result.theme->shapeSummary.isNotEmpty());
        CHECK(result.theme->meterSummary.isNotEmpty());

        for (const auto animationEffect : allAnimationEffects())
        {
            const auto& look = result.theme->effect(animationEffect);
            CHECK(look.available);
            CHECK(look.description.isNotEmpty());
        }
    }
}

TEST_CASE("The built-in themes come in menu order and Neon Grid is the default", "[appearance]")
{
    const auto& library = ThemeLibrary::builtIn();
    REQUIRE(library.getThemes().size() == 3);

    CHECK(library.getThemes()[0].id == "neon-grid");
    CHECK(library.getThemes()[0].name == "Neon Grid");
    CHECK(library.getThemes()[1].id == "acid-terminal");
    CHECK(library.getThemes()[1].name == "Acid Terminal");
    CHECK(library.getThemes()[2].id == "synth-chrome");
    CHECK(library.getThemes()[2].name == "Synth Chrome");

    CHECK(library.getDefault().id == "neon-grid");
    CHECK(library.getOrDefault("acid-terminal").id == "acid-terminal");
    CHECK(library.getOrDefault("theme-from-a-newer-version").id == "neon-grid");
    CHECK(library.getOrDefault({}).id == "neon-grid");
    CHECK(library.find("theme-from-a-newer-version") == nullptr);
}

TEST_CASE("Neon Grid has the agreed colours, fonts, shapes and meter", "[appearance]")
{
    const auto& theme = builtInTheme("neon-grid");

    CHECK(hexOf(theme, ColourRole::window) == "#07060B");
    CHECK(hexOf(theme, ColourRole::panel) == "#0E0C17");
    CHECK(hexOf(theme, ColourRole::panelAlt) == "#0A0912");
    CHECK(hexOf(theme, ColourRole::separator) == "#221E36");
    CHECK(hexOf(theme, ColourRole::text) == "#E9E6FF");
    CHECK(hexOf(theme, ColourRole::textSecondary) == "#8F8AAE");
    CHECK(hexOf(theme, ColourRole::accent) == "#00F0FF");
    CHECK(hexOf(theme, ColourRole::accentAlt) == "#FF2BD6");
    CHECK(hexOf(theme, ColourRole::highlight) == "#F8F32B");
    CHECK(hexOf(theme, ColourRole::loop) == "#9D7BFF");

    CHECK(theme.font(FontRole::label).family == "Chakra Petch");
    CHECK(theme.font(FontRole::numeric).family == "JetBrains Mono");
    CHECK(theme.font(FontRole::display).family == "JetBrains Mono");
    CHECK(theme.fontSummary() == "Chakra Petch, JetBrains Mono");

    CHECK(theme.shape.corners == CornerStyle::rounded);
    CHECK(theme.shape.cornerSmall == 3.0f);
    CHECK(theme.shape.cornerLarge == 6.0f);
    CHECK(theme.shape.glow > 0.0f);

    CHECK(theme.meter.style == MeterStyle::gradient);
    CHECK(theme.meter.colourAt(-60.0f).toHex() == "#00F0FF");
    CHECK(theme.meter.colourAt(-9.0f).toHex() == "#F8F32B");
    CHECK(theme.meter.colourAt(0.0f).toHex() == "#FF2BD6");
}

TEST_CASE("Acid Terminal has the agreed colours, fonts, shapes and meter", "[appearance]")
{
    const auto& theme = builtInTheme("acid-terminal");

    CHECK(hexOf(theme, ColourRole::window) == "#0B0D08");
    CHECK(hexOf(theme, ColourRole::panel) == "#12150E");
    CHECK(hexOf(theme, ColourRole::separator) == "#2A3020");
    CHECK(hexOf(theme, ColourRole::text) == "#EDEFE6");
    CHECK(hexOf(theme, ColourRole::textSecondary) == "#949A80");
    CHECK(hexOf(theme, ColourRole::accent) == "#D4FF00");
    CHECK(hexOf(theme, ColourRole::record) == "#FF3B30");
    CHECK(hexOf(theme, ColourRole::accentAlt) == "#7CE0FF");

    CHECK(theme.font(FontRole::title).family == "Space Grotesk");
    CHECK(theme.font(FontRole::heading).family == "Space Grotesk");
    CHECK(theme.font(FontRole::label).family == "IBM Plex Mono");
    CHECK(theme.font(FontRole::caption).family == "IBM Plex Mono");
    CHECK(theme.font(FontRole::numeric).family == "IBM Plex Mono");
    CHECK(theme.font(FontRole::display).family == "IBM Plex Mono");

    CHECK(theme.shape.corners == CornerStyle::sharp);
    CHECK(theme.shape.border == 2.0f);
    CHECK(theme.shape.labels == LabelStyle::terminal);
    CHECK(theme.shape.statusBar == StatusBarStyle::prompt);

    CHECK(theme.meter.style == MeterStyle::segments);
    CHECK(theme.meter.segmentHeight > 0.0f);
}

TEST_CASE("Synth Chrome has the agreed colours, fonts, shapes and meter", "[appearance]")
{
    const auto& theme = builtInTheme("synth-chrome");

    CHECK(hexOf(theme, ColourRole::window) == "#0F0820");
    CHECK(hexOf(theme, ColourRole::panel) == "#140B2A");
    CHECK(hexOf(theme, ColourRole::panelAlt) == "#180E30");
    CHECK(hexOf(theme, ColourRole::raised) == "#241645");
    CHECK(hexOf(theme, ColourRole::separator) == "#2E2050");
    CHECK(hexOf(theme, ColourRole::text) == "#F2EAFF");
    CHECK(hexOf(theme, ColourRole::textSecondary) == "#A397C4");
    CHECK(hexOf(theme, ColourRole::accent) == "#FF8A33");
    CHECK(hexOf(theme, ColourRole::accentAlt) == "#B07CFF");
    CHECK(hexOf(theme, ColourRole::record) == "#FF3D7F");
    CHECK(hexOf(theme, ColourRole::highlight) == "#5CE1E6");

    CHECK(theme.font(FontRole::title).family == "Michroma");
    CHECK(theme.font(FontRole::display).family == "Michroma");
    CHECK(theme.font(FontRole::label).family == "Rajdhani");

    CHECK(theme.shape.corners == CornerStyle::cut);
    CHECK(theme.shape.pan == PanStyle::arc);

    CHECK(theme.meter.style == MeterStyle::gradient);
    CHECK(theme.meter.colourAt(-60.0f).toHex() == "#B07CFF");
    CHECK(theme.meter.colourAt(-9.0f).toHex() == "#FF8A33");
    CHECK(theme.meter.colourAt(0.0f).toHex() == "#FF3D7F");
}

TEST_CASE("Gradient meters interpolate between stops; segment meters light zones", "[appearance]")
{
    MeterLook meter;
    meter.stops = {{0.0f, Colour{255, 0, 0, 255}},
                   {-10.0f, Colour{0, 255, 0, 255}},
                   {-20.0f, Colour{0, 0, 255, 255}}};

    meter.style = MeterStyle::gradient;
    CHECK(meter.colourAt(3.0f) == Colour{255, 0, 0, 255});
    CHECK(meter.colourAt(-5.0f) == Colour{128, 128, 0, 255});
    CHECK(meter.colourAt(-15.0f) == Colour{0, 128, 128, 255});
    CHECK(meter.colourAt(-90.0f) == Colour{0, 0, 255, 255});

    meter.style = MeterStyle::segments;
    CHECK(meter.colourAt(0.0f) == Colour{255, 0, 0, 255});
    CHECK(meter.colourAt(-0.5f) == Colour{0, 255, 0, 255});
    CHECK(meter.colourAt(-10.0f) == Colour{0, 255, 0, 255});
    CHECK(meter.colourAt(-15.0f) == Colour{0, 0, 255, 255});
    CHECK(meter.colourAt(-90.0f) == Colour{0, 0, 255, 255});
}

TEST_CASE("Tracks take the clip colours in turn", "[appearance]")
{
    ClipLook clips;
    clips.colours = {Colour{1, 0, 0, 255}, Colour{2, 0, 0, 255}, Colour{3, 0, 0, 255}};

    CHECK(clips.colourFor(0).red == 1);
    CHECK(clips.colourFor(2).red == 3);
    CHECK(clips.colourFor(3).red == 1);
    CHECK(clips.colourFor(-1).red == 3);
}
