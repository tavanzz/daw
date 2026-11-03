#pragma once

#include "daw/appearance/AnimationEffect.h"
#include "daw/appearance/Theme.h"

#include <juce_core/juce_core.h>

#include <array>
#include <optional>

namespace daw::appearance
{

/** How much animation the user wants, for every theme. */
enum class AnimationLevel
{
    full,    ///< every effect that is switched on
    reduced, ///< no moving backgrounds, glitches or panel transitions
    off      ///< nothing moves
};

/** False for the effects the level turns off whatever their switches say. */
[[nodiscard]] constexpr bool isAllowedAt(AnimationLevel level, AnimationEffect effect) noexcept
{
    switch (level)
    {
    case AnimationLevel::full:
        return true;
    case AnimationLevel::reduced:
        return effect != AnimationEffect::movingBackgrounds && effect != AnimationEffect::glitch &&
               effect != AnimationEffect::panelTransitions;
    case AnimationLevel::off:
        return false;
    }

    return false;
}

/** The user's animation settings (user settings, not part of any project). */
struct AnimationSettings
{
    AnimationLevel level = AnimationLevel::full;

    /** Where "switch all off" (Shift+Cmd+A) goes back to. Never off. */
    AnimationLevel levelBeforeOff = AnimationLevel::full;

    /** The switch of each effect, by AnimationEffect. */
    std::array<bool, animationEffectCount> switches{true, true, true, true, true, true};

    /** Cap the level at Reduced while the system asks apps to reduce motion. */
    bool followSystemReduceMotion = true;

    [[nodiscard]] bool isSwitchedOn(AnimationEffect effect) const noexcept;
    void setSwitchedOn(AnimationEffect effect, bool shouldBeOn) noexcept;

    /** Sets the level, remembering the last level other than off. */
    void setLevel(AnimationLevel newLevel) noexcept;

    /** Off; or, when already off, back to the level before. */
    void toggleAllOff() noexcept;

    /** The level in force: the chosen one, capped at Reduced when following a system
        that asks to reduce motion. */
    [[nodiscard]] AnimationLevel effectiveLevel(bool systemReducesMotion) const noexcept;

    /** Whether the effect runs: the theme draws it, it is switched on and the
        effective level allows it. */
    [[nodiscard]] bool isActive(AnimationEffect effect, const Theme& theme,
                                bool systemReducesMotion) const noexcept;

    /** True if at least one effect runs: the animation clock can stop otherwise. */
    [[nodiscard]] bool isAnythingActive(const Theme& theme,
                                        bool systemReducesMotion) const noexcept;

    friend bool operator==(const AnimationSettings&, const AnimationSettings&) = default;
};

/** The estimated drawing cost of the running effects, for the animation settings. */
struct AnimationLoad
{
    int points = 0;        ///< sum of the running effects' costs: low 1, medium 2, high 3
    int maximumPoints = 0; ///< with every effect of the theme running

    /** Low up to 4 points, medium up to 7, high above; empty when nothing runs. */
    std::optional<Cost> rating;
};

[[nodiscard]] constexpr int costPoints(Cost cost) noexcept
{
    return cost == Cost::low ? 1 : cost == Cost::medium ? 2 : 3;
}

[[nodiscard]] AnimationLoad animationLoad(const AnimationSettings& settings, const Theme& theme,
                                          bool systemReducesMotion) noexcept;

/** Names shown to the user, e.g. "Reduced", "Cursor glow", "Medium". */
[[nodiscard]] juce::String displayName(AnimationLevel level);
[[nodiscard]] juce::String displayName(AnimationEffect effect);
[[nodiscard]] juce::String displayName(Cost cost);

} // namespace daw::appearance
