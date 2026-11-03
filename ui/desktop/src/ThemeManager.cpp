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

    systemReducesMotion = systemMotion.asksToReduceMotion();
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
    clock.effectsChanged();
    listeners.call([](Listener& listener) { listener.animationSettingsChanged(); });
}

bool ThemeManager::isEffectActive(appearance::AnimationEffect effect) const
{
    return preferences.animations.isActive(effect, getTheme(), systemReducesMotion);
}

bool ThemeManager::systemAsksToReduceMotion() const noexcept
{
    return systemReducesMotion;
}

void ThemeManager::refreshSystemMotion()
{
    systemMotion.refresh();
}

void ThemeManager::setSystemReducesMotionForTesting(bool asksToReduce)
{
    systemMotionChanged(asksToReduce);
}

void ThemeManager::systemMotionChanged(bool asksToReduce)
{
    if (asksToReduce == systemReducesMotion)
        return;

    systemReducesMotion = asksToReduce;
    clock.effectsChanged();
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
    const auto previous = current;

    for (std::size_t index = 0; index < themes.size(); ++index)
        if (&themes[index] == &theme)
            current = index;

    // The theme that leaves keeps no images: they are painted again if it comes back.
    if (previous != current)
        lookAndFeels[previous]->getImages().clear();

    // JUCE tells every window, dialog, menu and tooltip on screen, which redraw in the
    // new theme.
    juce::LookAndFeel::setDefaultLookAndFeel(lookAndFeels[current].get());

    clock.effectsChanged();
    listeners.call([](Listener& listener) { listener.themeChanged(); });
}

void ThemeManager::save()
{
    if (saveCallback)
        saveCallback(preferences);
}

} // namespace daw::ui::desktop
