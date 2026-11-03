#include "daw/appearance/AnimationSettings.h"

namespace daw::appearance
{

bool AnimationSettings::isSwitchedOn(AnimationEffect effect) const noexcept
{
    return switches[static_cast<std::size_t>(effect)];
}

void AnimationSettings::setSwitchedOn(AnimationEffect effect, bool shouldBeOn) noexcept
{
    switches[static_cast<std::size_t>(effect)] = shouldBeOn;
}

void AnimationSettings::setLevel(AnimationLevel newLevel) noexcept
{
    if (newLevel != AnimationLevel::off)
        levelBeforeOff = newLevel;

    level = newLevel;
}

void AnimationSettings::toggleAllOff() noexcept
{
    if (level == AnimationLevel::off)
        level = levelBeforeOff;
    else
        setLevel(AnimationLevel::off);
}

AnimationLevel AnimationSettings::effectiveLevel(bool systemReducesMotion) const noexcept
{
    if (followSystemReduceMotion && systemReducesMotion && level == AnimationLevel::full)
        return AnimationLevel::reduced;

    return level;
}

bool AnimationSettings::isActive(AnimationEffect effect, const Theme& theme,
                                 bool systemReducesMotion) const noexcept
{
    return theme.effect(effect).available && isSwitchedOn(effect) &&
           isAllowedAt(effectiveLevel(systemReducesMotion), effect);
}

bool AnimationSettings::isAnythingActive(const Theme& theme,
                                         bool systemReducesMotion) const noexcept
{
    for (const auto effect : allAnimationEffects())
        if (isActive(effect, theme, systemReducesMotion))
            return true;

    return false;
}

AnimationLoad animationLoad(const AnimationSettings& settings, const Theme& theme,
                            bool systemReducesMotion) noexcept
{
    AnimationLoad load;

    for (const auto effect : allAnimationEffects())
    {
        const auto& look = theme.effect(effect);

        if (!look.available)
            continue;

        load.maximumPoints += costPoints(look.cost);

        if (settings.isActive(effect, theme, systemReducesMotion))
            load.points += costPoints(look.cost);
    }

    if (load.points > 7)
        load.rating = Cost::high;
    else if (load.points > 4)
        load.rating = Cost::medium;
    else if (load.points > 0)
        load.rating = Cost::low;

    return load;
}

juce::String displayName(AnimationLevel level)
{
    switch (level)
    {
    case AnimationLevel::full:
        return "Full";
    case AnimationLevel::reduced:
        return "Reduced";
    case AnimationLevel::off:
        return "Off";
    }

    return {};
}

juce::String displayName(AnimationEffect effect)
{
    switch (effect)
    {
    case AnimationEffect::animatedMeters:
        return "Animated meters";
    case AnimationEffect::cursorGlow:
        return "Cursor glow";
    case AnimationEffect::recordPulse:
        return "Record pulse";
    case AnimationEffect::movingBackgrounds:
        return "Moving backgrounds";
    case AnimationEffect::glitch:
        return "Glitch";
    case AnimationEffect::panelTransitions:
        return "Panel transitions";
    case AnimationEffect::count:
        break;
    }

    return {};
}

juce::String displayName(Cost cost)
{
    switch (cost)
    {
    case Cost::low:
        return "Low";
    case Cost::medium:
        return "Medium";
    case Cost::high:
        return "High";
    }

    return {};
}

} // namespace daw::appearance
