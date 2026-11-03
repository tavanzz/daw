#pragma once

#include "daw/appearance/AnimationSettings.h"
#include "daw/appearance/ThemeLibrary.h"

#include <juce_core/juce_core.h>

namespace daw::appearance
{

/**
    The user's appearance choices, kept in the user settings (never in a project):

        { "theme": "neon-grid",
          "animations": { "level": "full", "levelBeforeOff": "full",
                          "followSystemReduceMotion": true,
                          "effects": { "animatedMeters": true, "cursorGlow": true, ... } } }

    Reading never fails: anything missing, unknown or invalid takes its default, so
    settings from older or newer builds and hand edits still load. Theme ids are kept
    as written; ThemeLibrary::getOrDefault() resolves ones this build lacks.
*/
struct AppearancePreferences
{
    juce::String themeId = defaultThemeId;
    AnimationSettings animations;

    [[nodiscard]] juce::String toJson() const;
    [[nodiscard]] static AppearancePreferences fromJson(const juce::String& json);

    friend bool operator==(const AppearancePreferences&, const AppearancePreferences&) = default;
};

} // namespace daw::appearance
