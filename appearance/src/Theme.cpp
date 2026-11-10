#include "daw/appearance/Theme.h"

#include <algorithm>

namespace daw::appearance
{

namespace
{
constexpr std::array<std::string_view, colourRoleCount> colourRoleKeys{
    "window",        "panel",        "panelAlt",     "raised",     "inset",     "lane",
    "laneAlt",       "separator",    "gridBar",      "gridBeat",   "outline",   "text",
    "textSecondary", "textDisabled", "textOnAccent", "accent",     "accentAlt", "highlight",
    "focus",         "selection",    "playhead",     "loop",       "loopShade", "record",
    "mute",          "solo",         "clip",         "automation", "monitor",   "warning",
    "error",         "meterTrack",   "peakHold",     "curve",      "curveFill", "gainReduction",
    "shadow",        "keyWhite",     "keyBlack",     "keyRow"};

constexpr std::array<std::string_view, fontRoleCount> fontRoleKeys{"title",   "heading", "label",
                                                                   "caption", "numeric", "display"};

constexpr std::array<std::string_view, animationEffectCount> effectKeys{
    "animatedMeters",    "cursorGlow", "recordPulse",
    "movingBackgrounds", "glitch",     "panelTransitions"};
} // namespace

std::string_view keyOf(ColourRole role) noexcept
{
    return colourRoleKeys[static_cast<std::size_t>(role)];
}

std::string_view keyOf(FontRole role) noexcept
{
    return fontRoleKeys[static_cast<std::size_t>(role)];
}

std::string_view keyOf(AnimationEffect effect) noexcept
{
    return effectKeys[static_cast<std::size_t>(effect)];
}

Colour MeterLook::colourAt(float decibels) const noexcept
{
    if (stops.empty())
        return {};

    if (decibels >= stops.front().decibels)
        return stops.front().colour;

    for (std::size_t index = 1; index < stops.size(); ++index)
    {
        const auto& louder = stops[index - 1];
        const auto& quieter = stops[index];

        if (decibels >= quieter.decibels)
        {
            if (style == MeterStyle::segments)
                return quieter.colour;

            const float amount =
                (decibels - quieter.decibels) / (louder.decibels - quieter.decibels);
            return quieter.colour.mixedWith(louder.colour, amount);
        }
    }

    return stops.back().colour;
}

Colour ClipLook::colourFor(int trackIndex) const noexcept
{
    if (colours.empty())
        return {};

    const auto count = static_cast<int>(colours.size());
    return colours[static_cast<std::size_t>(((trackIndex % count) + count) % count)];
}

Colour Theme::participantColour(int index) const noexcept
{
    constexpr auto count = static_cast<int>(participantColourCount);
    return participants[static_cast<std::size_t>(((index % count) + count) % count)];
}

Colour Theme::colour(ColourRole role) const noexcept
{
    return colours[static_cast<std::size_t>(role)];
}

const FontSpec& Theme::font(FontRole role) const noexcept
{
    return fonts[static_cast<std::size_t>(role)];
}

const EffectLook& Theme::effect(AnimationEffect animationEffect) const noexcept
{
    return effects[static_cast<std::size_t>(animationEffect)];
}

juce::String Theme::fontSummary() const
{
    juce::StringArray families;

    for (const auto& spec : fonts)
        families.addIfNotAlreadyThere(spec.family);

    return families.joinIntoString(", ");
}

} // namespace daw::appearance
