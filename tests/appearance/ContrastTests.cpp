#include "daw/appearance/ColourMetrics.h"
#include "daw/appearance/ContrastRules.h"
#include "daw/appearance/ThemeLibrary.h"

#include <catch2/catch_approx.hpp>
#include <catch2/catch_test_macros.hpp>

#include <cmath>

using namespace daw::appearance;

namespace
{
Colour hex(const char* text)
{
    return *Colour::fromHex(text);
}

Theme copyOfNeonGrid()
{
    return *ThemeLibrary::builtIn().find("neon-grid");
}

void setColour(Theme& theme, ColourRole role, Colour colour)
{
    theme.colours[static_cast<std::size_t>(role)] = colour;
}
} // namespace

TEST_CASE("Contrast ratios and lightness match the WCAG and CIELAB references", "[appearance]")
{
    const auto black = hex("#000000");
    const auto white = hex("#FFFFFF");

    CHECK(contrastRatio(black, white) == Catch::Approx(21.0));
    CHECK(contrastRatio(white, black) == Catch::Approx(21.0));
    CHECK(contrastRatio(white, white) == Catch::Approx(1.0));

    // The well-known pair either side of WCAG's 4.5:1 for text on white.
    CHECK(contrastRatio(hex("#767676"), white) == Catch::Approx(4.54).margin(0.005));
    CHECK(contrastRatio(hex("#777777"), white) == Catch::Approx(4.48).margin(0.005));

    CHECK(relativeLuminance(hex("#FF0000")) == Catch::Approx(0.2126));
    CHECK(relativeLuminance(hex("#00FF00")) == Catch::Approx(0.7152));

    CHECK(lightness(black) == Catch::Approx(0.0).margin(1e-9));
    CHECK(lightness(white) == Catch::Approx(100.0).margin(1e-6));
    CHECK(lightness(hex("#777777")) == Catch::Approx(50.03).margin(0.01));
    CHECK(lightness(hex("#010101")) == Catch::Approx(0.27).margin(0.01)); // the linear part

    // sRGB red is L* 53.24, a* 80.09, b* 67.20 in CIELAB (D65).
    CHECK(colourDifference(black, white) == Catch::Approx(100.0).margin(1e-6));
    CHECK(colourDifference(hex("#FF0000"), black) == Catch::Approx(117.33).margin(0.05));
    CHECK(colourDifference(hex("#FF0000"), hex("#FF0000")) == 0.0);
}

TEST_CASE("Every built-in theme meets the contrast rules for every text and background pair",
          "[appearance]")
{
    for (const auto& theme : ThemeLibrary::builtIn().getThemes())
    {
        INFO(theme.name.toStdString());
        const auto problems = contrastProblems(theme);
        INFO(problems.joinIntoString("\n").toStdString());
        CHECK(problems.isEmpty());
    }
}

TEST_CASE("Status colours differ in lightness, not only in hue, in every built-in theme",
          "[appearance]")
{
    const auto statuses = statusRoles();
    REQUIRE(statuses.size() == 5);

    for (const auto& theme : ThemeLibrary::builtIn().getThemes())
    {
        for (std::size_t first = 0; first < statuses.size(); ++first)
        {
            for (std::size_t second = first + 1; second < statuses.size(); ++second)
            {
                INFO(theme.name.toStdString()
                     << ": " << keyOf(statuses[first]) << " and " << keyOf(statuses[second]));
                CHECK(std::abs(lightness(theme.colour(statuses[first])) -
                               lightness(theme.colour(statuses[second]))) >=
                      minimumStatusLightnessDifference);
            }
        }
    }
}

TEST_CASE("The contrast rules cover the surfaces text is drawn on", "[appearance]")
{
    int textOnPanel = 0;

    for (const auto& pair : colourPairs())
        if (pair.background == ColourRole::panel && pair.kind == ContrastKind::text &&
            (pair.foreground == ColourRole::text || pair.foreground == ColourRole::textSecondary))
            ++textOnPanel;

    CHECK(textOnPanel == 2);
    CHECK(minimumContrast(ContrastKind::text) == 4.5);
    CHECK(minimumContrast(ContrastKind::largeText) == 3.0);
    CHECK(minimumContrast(ContrastKind::graphic) == 3.0);
}

TEST_CASE("Unreadable text is reported with its ratio", "[appearance]")
{
    auto theme = copyOfNeonGrid();
    setColour(theme, ColourRole::textSecondary, hex("#2A2638"));

    const auto problems = contrastProblems(theme);
    INFO(problems.joinIntoString("\n").toStdString());

    CHECK(problems.contains("textSecondary on panel: 1.32:1, needs 4.50:1"));
}

TEST_CASE("Status colours of equal lightness are reported", "[appearance]")
{
    auto theme = copyOfNeonGrid();

    // Magenta and violet look equally light: they only differ in hue.
    setColour(theme, ColourRole::mute, hex("#9D7BFF"));
    REQUIRE(std::abs(lightness(hex("#9D7BFF")) - lightness(theme.colour(ColourRole::record))) <
            1.0);

    const auto problems = contrastProblems(theme);

    CHECK(problems.contains("record and mute: L* 0.6 apart, needs 10"));
}

TEST_CASE("A dim meter colour and an unreadable clip name are reported", "[appearance]")
{
    auto theme = copyOfNeonGrid();
    theme.meter.stops.back().colour = hex("#221E36");
    theme.clips.headerAlpha = 0.8f;

    const auto problems = contrastProblems(theme);
    INFO(problems.joinIntoString("\n").toStdString());

    CHECK(problems.contains("meter at -60 dB: 1.14:1, needs 3.00:1"));
    CHECK(problems.joinIntoString("\n").contains("clip colour 2 on lane, name"));
}

TEST_CASE("Participant colours that look alike or vanish on a lane are reported", "[appearance]")
{
    auto theme = copyOfNeonGrid();
    theme.participants[5] = theme.participants[1].mixedWith(hex("#FFFFFF"), 0.05f);
    theme.participants[7] = hex("#221E36");

    const auto problems = contrastProblems(theme).joinIntoString("\n");
    INFO(problems.toStdString());

    CHECK(problems.contains("participant colour 2 and 6: Delta E"));
    CHECK(problems.contains("participant colour 8 on lane"));
}

TEST_CASE("Every built-in theme has eight participant colours told apart at a glance", "[appearance]")
{
    for (const auto& theme : ThemeLibrary::builtIn().getThemes())
        for (std::size_t first = 0; first < participantColourCount; ++first)
            for (std::size_t second = first + 1; second < participantColourCount; ++second)
            {
                INFO(theme.id << " " << first << " " << second);
                CHECK(colourDifference(theme.participants[first], theme.participants[second])
                      >= minimumParticipantDifference);
            }
}

TEST_CASE("Text on a fill takes whichever text colour reads better", "[appearance]")
{
    const auto theme = copyOfNeonGrid();

    CHECK(textRoleOn(theme, theme.colour(ColourRole::accent)) == ColourRole::textOnAccent);
    CHECK(textRoleOn(theme, theme.colour(ColourRole::automation)) == ColourRole::text);
    CHECK(textRoleOn(theme, hex("#FFFFFF")) == ColourRole::textOnAccent);
    CHECK(textRoleOn(theme, hex("#000000")) == ColourRole::text);
}
