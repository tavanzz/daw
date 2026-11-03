#include "daw/appearance/AnimationSettings.h"
#include "daw/appearance/AppearancePreferences.h"
#include "daw/appearance/ThemeLibrary.h"

#include <catch2/catch_test_macros.hpp>

using namespace daw::appearance;

namespace
{
const Theme& neonGrid()
{
    return *ThemeLibrary::builtIn().find("neon-grid");
}

constexpr bool noSystemRequest = false;
constexpr bool systemReducesMotion = true;
} // namespace

TEST_CASE("Animations default to full with every effect on and the system followed", "[appearance]")
{
    const AnimationSettings settings;

    CHECK(settings.level == AnimationLevel::full);
    CHECK(settings.followSystemReduceMotion);

    for (const auto effect : allAnimationEffects())
    {
        CHECK(settings.isSwitchedOn(effect));
        CHECK(settings.isActive(effect, neonGrid(), noSystemRequest));
    }
}

TEST_CASE("Reduced turns off moving backgrounds, glitch and panel transitions", "[appearance]")
{
    AnimationSettings settings;
    settings.setLevel(AnimationLevel::reduced);

    CHECK(settings.isActive(AnimationEffect::animatedMeters, neonGrid(), noSystemRequest));
    CHECK(settings.isActive(AnimationEffect::cursorGlow, neonGrid(), noSystemRequest));
    CHECK(settings.isActive(AnimationEffect::recordPulse, neonGrid(), noSystemRequest));
    CHECK_FALSE(settings.isActive(AnimationEffect::movingBackgrounds, neonGrid(), noSystemRequest));
    CHECK_FALSE(settings.isActive(AnimationEffect::glitch, neonGrid(), noSystemRequest));
    CHECK_FALSE(settings.isActive(AnimationEffect::panelTransitions, neonGrid(), noSystemRequest));

    // The switches themselves are kept, for when the level goes back to full.
    CHECK(settings.isSwitchedOn(AnimationEffect::glitch));
}

TEST_CASE("Off stops every effect and the switches still apply below it", "[appearance]")
{
    AnimationSettings settings;
    settings.setSwitchedOn(AnimationEffect::cursorGlow, false);

    CHECK_FALSE(settings.isActive(AnimationEffect::cursorGlow, neonGrid(), noSystemRequest));
    CHECK(settings.isAnythingActive(neonGrid(), noSystemRequest));

    settings.setLevel(AnimationLevel::off);
    CHECK_FALSE(settings.isAnythingActive(neonGrid(), noSystemRequest));

    settings.setLevel(AnimationLevel::full);
    for (const auto effect : allAnimationEffects())
        settings.setSwitchedOn(effect, false);
    CHECK_FALSE(settings.isAnythingActive(neonGrid(), noSystemRequest));
}

TEST_CASE("The system's reduce motion caps the level at reduced while followed", "[appearance]")
{
    AnimationSettings settings;

    CHECK(settings.effectiveLevel(systemReducesMotion) == AnimationLevel::reduced);
    CHECK_FALSE(settings.isActive(AnimationEffect::glitch, neonGrid(), systemReducesMotion));
    CHECK(settings.isActive(AnimationEffect::cursorGlow, neonGrid(), systemReducesMotion));

    settings.setLevel(AnimationLevel::off);
    CHECK(settings.effectiveLevel(systemReducesMotion) == AnimationLevel::off);

    settings.setLevel(AnimationLevel::full);
    settings.followSystemReduceMotion = false;
    CHECK(settings.effectiveLevel(systemReducesMotion) == AnimationLevel::full);
    CHECK(settings.isActive(AnimationEffect::glitch, neonGrid(), systemReducesMotion));
}

TEST_CASE("Switching all off goes back to the level before", "[appearance]")
{
    AnimationSettings settings;
    settings.setLevel(AnimationLevel::reduced);

    settings.toggleAllOff();
    CHECK(settings.level == AnimationLevel::off);

    settings.toggleAllOff();
    CHECK(settings.level == AnimationLevel::reduced);

    // Choosing off in the panel, then the shortcut: back to the last level used.
    settings.setLevel(AnimationLevel::full);
    settings.setLevel(AnimationLevel::off);
    settings.toggleAllOff();
    CHECK(settings.level == AnimationLevel::full);
}

TEST_CASE("An effect the theme does not draw never runs", "[appearance]")
{
    auto theme = neonGrid();
    theme.effects[static_cast<std::size_t>(AnimationEffect::glitch)].available = false;

    const AnimationSettings settings;
    CHECK_FALSE(settings.isActive(AnimationEffect::glitch, theme, noSystemRequest));
    CHECK(settings.isActive(AnimationEffect::cursorGlow, theme, noSystemRequest));
}

TEST_CASE("The animation load adds up the running effects' costs", "[appearance]")
{
    // Neon Grid: meters medium, cursor low, record low, backgrounds medium, glitch low,
    // transitions low (as measured, docs/PERFORMANCE.md).
    AnimationSettings settings;

    const auto full = animationLoad(settings, neonGrid(), noSystemRequest);
    CHECK(full.points == 8);
    CHECK(full.maximumPoints == 8);
    CHECK(full.rating == Cost::high);

    settings.setLevel(AnimationLevel::reduced);
    const auto reduced = animationLoad(settings, neonGrid(), noSystemRequest);
    CHECK(reduced.points == 4);
    CHECK(reduced.rating == Cost::low);

    settings.setLevel(AnimationLevel::full);
    settings.setSwitchedOn(AnimationEffect::movingBackgrounds, false);
    const auto withoutBackgrounds = animationLoad(settings, neonGrid(), noSystemRequest);
    CHECK(withoutBackgrounds.points == 6);
    CHECK(withoutBackgrounds.rating == Cost::medium);

    settings.setLevel(AnimationLevel::off);
    const auto off = animationLoad(settings, neonGrid(), noSystemRequest);
    CHECK(off.points == 0);
    CHECK_FALSE(off.rating.has_value());
}

TEST_CASE("Appearance preferences survive a round trip through JSON", "[appearance]")
{
    AppearancePreferences preferences;
    preferences.themeId = "synth-chrome";
    preferences.animations.setLevel(AnimationLevel::reduced);
    preferences.animations.setLevel(AnimationLevel::off);
    preferences.animations.followSystemReduceMotion = false;
    preferences.animations.setSwitchedOn(AnimationEffect::glitch, false);

    const auto restored = AppearancePreferences::fromJson(preferences.toJson());

    CHECK(restored == preferences);
    CHECK(restored.animations.levelBeforeOff == AnimationLevel::reduced);
    CHECK(preferences.toJson() ==
          R"({"theme":"synth-chrome","animations":{"level":"off","levelBeforeOff":"reduced",)"
          R"("followSystemReduceMotion":false,"effects":{"animatedMeters":true,)"
          R"("cursorGlow":true,"recordPulse":true,"movingBackgrounds":true,"glitch":false,)"
          R"("panelTransitions":true}}})");
}

TEST_CASE("Missing or invalid appearance preferences take their defaults", "[appearance]")
{
    const AppearancePreferences defaults;

    CHECK(AppearancePreferences::fromJson({}) == defaults);
    CHECK(AppearancePreferences::fromJson("not json") == defaults);
    CHECK(defaults.themeId == "neon-grid");

    const auto partial = AppearancePreferences::fromJson(
        R"({"theme": 7, "animations": {"level": "dazzling", "levelBeforeOff": "off",
             "followSystemReduceMotion": "yes", "effects": {"glitch": false, "sparkles": true}},
             "fromANewerVersion": 1})");

    CHECK(partial.themeId == "neon-grid");
    CHECK(partial.animations.level == AnimationLevel::full);
    CHECK(partial.animations.levelBeforeOff == AnimationLevel::full);
    CHECK(partial.animations.followSystemReduceMotion);
    CHECK_FALSE(partial.animations.isSwitchedOn(AnimationEffect::glitch));
    CHECK(partial.animations.isSwitchedOn(AnimationEffect::cursorGlow));

    // An id this build lacks is kept (a newer build may have it) and resolved on use.
    const auto unknown = AppearancePreferences::fromJson(R"({"theme": "aurora"})");
    CHECK(unknown.themeId == "aurora");
    CHECK(ThemeLibrary::builtIn().getOrDefault(unknown.themeId).id == "neon-grid");
}
