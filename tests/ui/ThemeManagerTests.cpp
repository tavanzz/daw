#include "daw/ui/desktop/ThemeManager.h"

#include <catch2/catch_test_macros.hpp>

#include <optional>

using namespace daw;
using ui::desktop::ThemeManager;

namespace
{
// A window that counts how often it was told its look and feel changed.
class CountingWindow final : public juce::Component
{
public:
    CountingWindow()
    {
        setSize(200, 100);
        addToDesktop(0);
    }

    ~CountingWindow() override { removeFromDesktop(); }

    void lookAndFeelChanged() override { ++changes; }

    int changes = 0;
};

class RecordingListener final : public ThemeManager::Listener
{
public:
    void themeChanged() override { ++themeChanges; }
    void animationSettingsChanged() override { ++animationChanges; }

    int themeChanges = 0;
    int animationChanges = 0;
};
} // namespace

TEST_CASE("The theme manager applies the saved theme, or Neon Grid for an unknown one", "[ui]")
{
    appearance::AppearancePreferences saved;
    saved.themeId = "synth-chrome";

    {
        ThemeManager themes(saved);
        CHECK(themes.getTheme().id == "synth-chrome");
        CHECK(&juce::LookAndFeel::getDefaultLookAndFeel() == &themes.getLookAndFeel());
        CHECK(themes.getLookAndFeel().getTheme().id == "synth-chrome");
    }

    saved.themeId = "theme-from-a-newer-version";
    std::optional<appearance::AppearancePreferences> stored;
    ThemeManager themes(saved, [&stored](const auto& preferences) { stored = preferences; });

    CHECK(themes.getTheme().id == "neon-grid");
    CHECK_FALSE(stored.has_value()); // nothing is written until the user chooses
    CHECK(themes.getPreferences().themeId == "theme-from-a-newer-version");
}

TEST_CASE("Switching themes restyles every open window at once and saves the choice", "[ui]")
{
    std::optional<appearance::AppearancePreferences> stored;
    ThemeManager themes({}, [&stored](const auto& preferences) { stored = preferences; });
    RecordingListener listener;
    themes.addListener(listener);

    CountingWindow window;
    juce::Component child;
    window.addAndMakeVisible(child);
    window.changes = 0;

    themes.setTheme("acid-terminal");

    CHECK(themes.getTheme().id == "acid-terminal");
    CHECK(&juce::LookAndFeel::getDefaultLookAndFeel() == &themes.getLookAndFeel());
    CHECK(&child.getLookAndFeel() == &themes.getLookAndFeel());
    CHECK(window.changes == 1);
    CHECK(listener.themeChanges == 1);
    REQUIRE(stored.has_value());
    CHECK(stored->themeId == "acid-terminal");

    // The same theme again, or one that does not exist: nothing happens.
    themes.setTheme("acid-terminal");
    themes.setTheme("no-such-theme");
    CHECK(window.changes == 1);
    CHECK(listener.themeChanges == 1);

    themes.removeListener(listener);
}

TEST_CASE("Animation settings changes are saved and announced", "[ui]")
{
    std::optional<appearance::AppearancePreferences> stored;
    ThemeManager themes({}, [&stored](const auto& preferences) { stored = preferences; });
    RecordingListener listener;
    themes.addListener(listener);

    auto settings = themes.getPreferences().animations;
    settings.setLevel(appearance::AnimationLevel::reduced);
    themes.setAnimationSettings(settings);

    CHECK(listener.animationChanges == 1);
    CHECK(listener.themeChanges == 0);
    REQUIRE(stored.has_value());
    CHECK(stored->animations.level == appearance::AnimationLevel::reduced);

    themes.setAnimationSettings(settings);
    CHECK(listener.animationChanges == 1);

    themes.removeListener(listener);
}

TEST_CASE("The theme manager leaves no look and feel behind", "[ui]")
{
    {
        ThemeManager themes({});
        CHECK(dynamic_cast<ui::desktop::ThemedLookAndFeel*>(
                  &juce::LookAndFeel::getDefaultLookAndFeel()) != nullptr);
    }

    CHECK(dynamic_cast<ui::desktop::ThemedLookAndFeel*>(
              &juce::LookAndFeel::getDefaultLookAndFeel()) == nullptr);
}
