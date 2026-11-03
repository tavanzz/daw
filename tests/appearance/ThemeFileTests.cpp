#include "daw/appearance/ThemeFile.h"
#include "daw/appearance/ThemeLibrary.h"

#include <catch2/catch_test_macros.hpp>

#include <functional>

using namespace daw::appearance;

namespace
{
// A complete theme to break in one place at a time.
juce::var completeTheme()
{
    return juce::JSON::parse(builtInThemeFiles().front().json);
}

juce::StringArray errorsAfter(const std::function<void(juce::var&)>& change)
{
    auto json = completeTheme();
    change(json);
    const auto result = readTheme(juce::JSON::toString(json));
    CHECK_FALSE(result.theme.has_value());
    return result.errors;
}

juce::DynamicObject& objectAt(juce::var& json, const char* key)
{
    return *json[key].getDynamicObject();
}
} // namespace

TEST_CASE("A complete theme reads without errors", "[appearance]")
{
    const auto result = readTheme(juce::JSON::toString(completeTheme()));

    INFO(result.errors.joinIntoString("\n"));
    REQUIRE(result.errors.isEmpty());
    REQUIRE(result.theme.has_value());
    CHECK(result.theme->id == "neon-grid");
    CHECK(result.theme->name == "Neon Grid");
}

TEST_CASE("A theme without a colour role is refused, naming the role", "[appearance]")
{
    const auto errors =
        errorsAfter([](juce::var& json) { objectAt(json, "colours").removeProperty("panel"); });

    CHECK(errors == juce::StringArray("colours.panel: missing"));
}

TEST_CASE("A theme without a font role or an effect is refused", "[appearance]")
{
    CHECK(errorsAfter([](juce::var& json) { objectAt(json, "fonts").removeProperty("display"); }) ==
          juce::StringArray("fonts.display: missing"));

    CHECK(
        errorsAfter([](juce::var& json) { objectAt(json, "effects").removeProperty("glitch"); }) ==
        juce::StringArray("effects.glitch: missing"));

    CHECK(errorsAfter([](juce::var& json)
                      { json["fonts"]["title"].getDynamicObject()->removeProperty("size"); }) ==
          juce::StringArray("fonts.title.size: missing"));
}

TEST_CASE("Unknown and misspelt theme fields are errors", "[appearance]")
{
    const auto errors = errorsAfter(
        [](juce::var& json)
        {
            objectAt(json, "colours").setProperty("panell", "#000000");
            objectAt(json, "shape").setProperty("radius", 4);
            json.getDynamicObject()->setProperty("author", "someone");
        });

    CHECK(errors.contains("colours.panell: unknown field"));
    CHECK(errors.contains("shape.radius: unknown field"));
    CHECK(errors.contains("author: unknown field"));
    CHECK(errors.size() == 3);
}

TEST_CASE("Theme colours must be palette names, hex colours or formulas", "[appearance]")
{
    const auto errors = errorsAfter(
        [](juce::var& json)
        {
            auto& colours = objectAt(json, "colours");
            colours.setProperty("panel", "#12345");
            colours.setProperty("text", "silver");
            colours.setProperty("outline", juce::JSON::parse(R"({ "mix": ["line", "text"] })"));
            colours.setProperty("shadow", juce::JSON::parse(R"({ "alpha": ["line", 1.5] })"));
            objectAt(json, "palette").setProperty("cyan", "cyan");
        });

    CHECK(
        errors.contains("colours.panel: \"#12345\" is not a colour, \"#RRGGBB\" or \"#RRGGBBAA\""));
    CHECK(errors.contains("colours.text: \"silver\" is not in the palette"));
    CHECK(errors.contains("colours.outline: \"mix\" takes [colour, colour, amount]"));
    CHECK(errors.contains("colours.shadow.alpha[1]: must be from 0 to 1"));
    CHECK(errors.contains("palette.cyan: must be a colour, \"#RRGGBB\" or \"#RRGGBBAA\""));
}

TEST_CASE("Mix and alpha formulas derive colours from the palette", "[appearance]")
{
    auto json = completeTheme();
    auto& colours = objectAt(json, "colours");
    colours.setProperty("outline", juce::JSON::parse(R"({ "mix": ["#000000", "#FFFFFF", 0.5] })"));
    colours.setProperty("shadow", juce::JSON::parse(R"({ "alpha": ["cyan", 0.5] })"));
    colours.setProperty("gridBar",
                        juce::JSON::parse(R"({ "mix": [{ "mix": ["#000000", "#FFFFFF", 0.5] },
                                                        "#000000", 0.5] })"));

    const auto result = readTheme(juce::JSON::toString(json));
    REQUIRE(result.theme.has_value());

    CHECK(result.theme->colour(ColourRole::outline) == Colour{128, 128, 128, 255});
    CHECK(result.theme->colour(ColourRole::shadow) == Colour{0x00, 0xf0, 0xff, 128});
    CHECK(result.theme->colour(ColourRole::gridBar) == Colour{64, 64, 64, 255});
}

TEST_CASE("Segment meters need their segment sizes; gradient meters must not have them",
          "[appearance]")
{
    const auto missing = errorsAfter([](juce::var& json)
                                     { objectAt(json, "meter").setProperty("style", "segments"); });
    CHECK(missing.contains("meter.segmentHeight: missing"));
    CHECK(missing.contains("meter.segmentGap: missing"));

    const auto extra = errorsAfter([](juce::var& json)
                                   { objectAt(json, "meter").setProperty("segmentHeight", 3); });
    CHECK(extra == juce::StringArray("meter.segmentHeight: unknown field"));
}

TEST_CASE("Meter stops must run from the loudest down", "[appearance]")
{
    const auto errors = errorsAfter(
        [](juce::var& json)
        {
            objectAt(json, "meter")
                .setProperty("stops", juce::JSON::parse(R"([ { "db": -30, "colour": "cyan" },
                                            { "db": 0, "colour": "magenta" } ])"));
        });

    CHECK(errors == juce::StringArray("meter.stops[1]: stops must run from the loudest down"));
}

TEST_CASE("An effect a theme does not draw needs no cost", "[appearance]")
{
    auto withoutGlitch = completeTheme();
    objectAt(withoutGlitch, "effects")
        .setProperty("glitch", juce::JSON::parse(R"({ "available": false })"));

    const auto result = readTheme(juce::JSON::toString(withoutGlitch));
    REQUIRE(result.theme.has_value());
    CHECK_FALSE(result.theme->effect(AnimationEffect::glitch).available);

    CHECK(errorsAfter(
              [](juce::var& json)
              {
                  objectAt(json, "effects")
                      .setProperty("glitch", juce::JSON::parse(R"({ "available": true })"));
              }) == juce::StringArray({"effects.glitch.cost: missing",
                                       "effects.glitch.description: missing"}));
}

TEST_CASE("Every problem in a theme is reported at once", "[appearance]")
{
    const auto errors = errorsAfter(
        [](juce::var& json)
        {
            json.getDynamicObject()->removeProperty("name");
            objectAt(json, "shape").setProperty("corners", "round");
            objectAt(json, "fonts").removeProperty("caption");
        });

    CHECK(errors.contains("name: missing"));
    CHECK(errors.contains("shape.corners: must be one of \"rounded\", \"sharp\", \"cut\""));
    CHECK(errors.contains("fonts.caption: missing"));
    CHECK(errors.size() == 3);
}

TEST_CASE("Text that is not a JSON object is not a theme", "[appearance]")
{
    CHECK_FALSE(readTheme("not json").theme.has_value());
    CHECK(readTheme("[1, 2]").errors == juce::StringArray("the theme must be a JSON object"));
}
