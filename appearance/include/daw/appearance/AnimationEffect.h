#pragma once

#include <array>
#include <cstddef>
#include <string_view>

namespace daw::appearance
{

/** The animated effects the user can switch on and off, the same for every theme;
    each theme draws them its own way (see the themes' "effects"). */
enum class AnimationEffect
{
    animatedMeters,    ///< glow, LED afterglow, smooth peak-hold fall
    cursorGlow,        ///< the playhead's pulse or trail, the status-bar cursor
    recordPulse,       ///< the Rec button and recording clips
    movingBackgrounds, ///< scanlines, hazard stripes, perspective grid
    glitch,            ///< the logo's flicker, glitch or chrome sweep
    panelTransitions,  ///< the mixer sliding, panels fading, theme crossfades
    count
};

inline constexpr std::size_t animationEffectCount =
    static_cast<std::size_t>(AnimationEffect::count);

/** Every effect, in the order the animation settings list them. */
[[nodiscard]] constexpr std::array<AnimationEffect, animationEffectCount>
allAnimationEffects() noexcept
{
    return {AnimationEffect::animatedMeters, AnimationEffect::cursorGlow,
            AnimationEffect::recordPulse,    AnimationEffect::movingBackgrounds,
            AnimationEffect::glitch,         AnimationEffect::panelTransitions};
}

/** The effect's key in theme files and the user settings, e.g. "cursorGlow". */
[[nodiscard]] std::string_view keyOf(AnimationEffect effect) noexcept;

} // namespace daw::appearance
