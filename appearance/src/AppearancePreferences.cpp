#include "daw/appearance/AppearancePreferences.h"

#include <array>
#include <optional>
#include <utility>

namespace daw::appearance
{

namespace
{
namespace json
{
constexpr const char* theme = "theme";
constexpr const char* animations = "animations";
constexpr const char* level = "level";
constexpr const char* levelBeforeOff = "levelBeforeOff";
constexpr const char* followSystemReduceMotion = "followSystemReduceMotion";
constexpr const char* effects = "effects";
} // namespace json

constexpr std::array<std::pair<const char*, AnimationLevel>, 3> levelNames{
    {{"full", AnimationLevel::full},
     {"reduced", AnimationLevel::reduced},
     {"off", AnimationLevel::off}}};

const char* nameOf(AnimationLevel level)
{
    for (const auto& [name, value] : levelNames)
        if (value == level)
            return name;

    return "full";
}

std::optional<AnimationLevel> levelNamed(const juce::var& value)
{
    for (const auto& [name, level] : levelNames)
        if (value.isString() && value.toString() == name)
            return level;

    return std::nullopt;
}

juce::Identifier keyFor(AnimationEffect effect)
{
    return juce::String(std::string(keyOf(effect)));
}
} // namespace

juce::String AppearancePreferences::toJson() const
{
    auto* effectSwitches = new juce::DynamicObject();
    for (const auto effect : allAnimationEffects())
        effectSwitches->setProperty(keyFor(effect), animations.isSwitchedOn(effect));

    auto* animationObject = new juce::DynamicObject();
    animationObject->setProperty(json::level, nameOf(animations.level));
    animationObject->setProperty(json::levelBeforeOff, nameOf(animations.levelBeforeOff));
    animationObject->setProperty(json::followSystemReduceMotion,
                                 animations.followSystemReduceMotion);
    animationObject->setProperty(json::effects, juce::var(effectSwitches));

    auto* root = new juce::DynamicObject();
    root->setProperty(json::theme, themeId);
    root->setProperty(json::animations, juce::var(animationObject));

    return juce::JSON::toString(juce::var(root),
                                juce::JSON::FormatOptions{}.withSpacing(juce::JSON::Spacing::none));
}

AppearancePreferences AppearancePreferences::fromJson(const juce::String& text)
{
    AppearancePreferences preferences;
    const auto root = juce::JSON::parse(text);

    if (const auto theme = root[json::theme]; theme.isString() && theme.toString().isNotEmpty())
        preferences.themeId = theme.toString();

    const auto animationObject = root[json::animations];
    auto& animations = preferences.animations;

    if (const auto level = levelNamed(animationObject[json::level]))
        animations.level = *level;

    if (const auto level = levelNamed(animationObject[json::levelBeforeOff]);
        level && *level != AnimationLevel::off)
        animations.levelBeforeOff = *level;

    if (const auto follow = animationObject[json::followSystemReduceMotion]; follow.isBool())
        animations.followSystemReduceMotion = static_cast<bool>(follow);

    const auto effectSwitches = animationObject[json::effects];
    for (const auto effect : allAnimationEffects())
        if (const auto value = effectSwitches[keyFor(effect)]; value.isBool())
            animations.setSwitchedOn(effect, static_cast<bool>(value));

    return preferences;
}

} // namespace daw::appearance
