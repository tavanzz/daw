#pragma once

#include "daw/appearance/AppearancePreferences.h"
#include "daw/appearance/ThemeLibrary.h"
#include "daw/ui/desktop/AnimationClock.h"
#include "daw/ui/desktop/SystemMotion.h"
#include "daw/ui/desktop/ThemeFonts.h"
#include "daw/ui/desktop/ThemedLookAndFeel.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <functional>
#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/**
    The appearance of the desktop app: the current theme as the default look and feel
    of every window, the animation settings with the system's reduce motion, and the
    animation clock. Changing the theme restyles every open window at once (no
    restart); each change of the preferences is passed to the owner to store in the
    user settings, never in the project.

    Owned by the application and created before any window, destroyed after the last
    one: windows draw with the look and feels it owns. Message thread.
*/
class ThemeManager final
{
public:
    /** Notified after the theme or the animation settings change. */
    class Listener
    {
    public:
        virtual ~Listener() = default;
        /** Just before the theme changes: the old look is still on screen. */
        virtual void themeAboutToChange() {}
        virtual void themeChanged() {}
        virtual void animationSettingsChanged() {}
    };

    using PreferencesSaver = std::function<void(const appearance::AppearancePreferences&)>;

    /** Applies the preferences' theme (or the default one if this build lacks it). */
    explicit ThemeManager(
        appearance::AppearancePreferences preferencesToUse, PreferencesSaver saver = {},
        const appearance::ThemeLibrary& library = appearance::ThemeLibrary::builtIn());
    ~ThemeManager();

    [[nodiscard]] const appearance::Theme& getTheme() const noexcept;
    [[nodiscard]] ThemedLookAndFeel& getLookAndFeel() noexcept;
    [[nodiscard]] const ThemeFonts& getFonts() const noexcept { return fonts; }
    [[nodiscard]] const appearance::ThemeLibrary& getLibrary() const noexcept { return library; }
    [[nodiscard]] const appearance::AppearancePreferences& getPreferences() const noexcept
    {
        return preferences;
    }

    /** Switches every window to the theme now and saves the choice. Unknown ids are
        ignored. */
    void setTheme(const juce::String& themeId);

    void setAnimationSettings(const appearance::AnimationSettings& settings);

    /** Whether an effect runs now: the theme draws it, its switch is on and the level
        in force (the system's reduce motion included) allows it. */
    [[nodiscard]] bool isEffectActive(appearance::AnimationEffect effect) const;

    [[nodiscard]] bool systemAsksToReduceMotion() const noexcept;

    /** Reads the system's reduce motion again (when the app becomes active). */
    void refreshSystemMotion();

    [[nodiscard]] AnimationClock& getAnimationClock() noexcept { return clock; }

    /** For tests: as if the system had changed its reduce-motion setting. */
    void setSystemReducesMotionForTesting(bool asksToReduce);

    void addListener(Listener& listener);
    void removeListener(Listener& listener);

private:
    void apply();
    void save();
    void systemMotionChanged(bool asksToReduce);

    const appearance::ThemeLibrary& library;
    appearance::AppearancePreferences preferences;
    PreferencesSaver saveCallback;
    ThemeFonts fonts;
    std::vector<std::unique_ptr<ThemedLookAndFeel>> lookAndFeels; // one per library theme
    std::size_t current = 0;
    juce::ListenerList<Listener> listeners;
    bool systemReducesMotion = false;
    AnimationClock clock{[this](appearance::AnimationEffect effect)
                         {
                             return isEffectActive(effect);
                         }};
    SystemMotion systemMotion{[this](bool asksToReduce)
                              {
                                  systemMotionChanged(asksToReduce);
                              }};

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeManager)
};

} // namespace daw::ui::desktop
