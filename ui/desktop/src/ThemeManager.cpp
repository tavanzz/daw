#include "daw/ui/desktop/ThemeManager.h"

namespace daw::ui::desktop
{

ThemeManager::ThemeManager(appearance::AppearancePreferences preferencesToUse,
                           PreferencesSaver saver, const appearance::ThemeLibrary& themeLibrary)
    : library(themeLibrary), preferences(std::move(preferencesToUse)),
      saveCallback(std::move(saver))
{
    for (const auto& theme : library.getThemes())
        lookAndFeels.push_back(ThemedLookAndFeel::make(theme, fonts));

    apply();
}

ThemeManager::~ThemeManager()
{
    // Windows are gone by now; nothing may keep drawing with a look and feel freed here.
    juce::LookAndFeel::setDefaultLookAndFeel(nullptr);
}

const appearance::Theme& ThemeManager::getTheme() const noexcept
{
    return library.getThemes()[current];
}

ThemedLookAndFeel& ThemeManager::getLookAndFeel() noexcept
{
    return *lookAndFeels[current];
}

void ThemeManager::setTheme(const juce::String& themeId)
{
    if (library.find(themeId) == nullptr || themeId == getTheme().id)
        return;

    preferences.themeId = themeId;
    apply();
    save();
}

void ThemeManager::setAnimationSettings(const appearance::AnimationSettings& settings)
{
    if (settings == preferences.animations)
        return;

    preferences.animations = settings;
    save();
    listeners.call([](Listener& listener) { listener.animationSettingsChanged(); });
}

void ThemeManager::addListener(Listener& listener)
{
    listeners.add(&listener);
}

void ThemeManager::removeListener(Listener& listener)
{
    listeners.remove(&listener);
}

void ThemeManager::apply()
{
    const auto& theme = library.getOrDefault(preferences.themeId);
    const auto& themes = library.getThemes();

    for (std::size_t index = 0; index < themes.size(); ++index)
        if (&themes[index] == &theme)
            current = index;

    // JUCE tells every window, dialog, menu and tooltip on screen, which redraw in the
    // new theme.
    juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeels[current].get());

    listeners.call([](Listener& listener) { listener.themeChanged(); });
}

void ThemeManager::save()
{
    if (saveCallback)
        saveCallback(preferences);
}

} // namespace daw::ui::desktop
