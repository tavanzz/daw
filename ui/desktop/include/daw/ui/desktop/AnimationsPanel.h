#pragma once

#include "daw/ui/desktop/ThemeManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <array>

namespace daw::ui::desktop
{

/** The animation settings: the level (Full, Reduced, Off), a switch per effect with its
    estimated cost and what it looks like in the theme in use, the overall load, and
    whether to follow the system's "Reduce motion". The settings hold for every theme. */
class AnimationsPanel final : public juce::Component, private ThemeManager::Listener
{
public:
    explicit AnimationsPanel(ThemeManager& themeManager);
    ~AnimationsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

private:
    struct EffectRow
    {
        juce::ToggleButton toggle;
        juce::Label cost;
        juce::Label description;
    };

    // The load as a row of segments, one per point of cost.
    class LoadBar final : public juce::Component
    {
    public:
        void setLoad(int litPoints, int totalPoints);
        void paint(juce::Graphics& g) override;

    private:
        int lit = 0;
        int total = 1;
    };

    void themeChanged() override;
    void animationSettingsChanged() override;
    void show();
    void change(const std::function<void(appearance::AnimationSettings&)>& edit);

    ThemeManager& themes;
    juce::Label heading;
    juce::Label levelLabel;
    std::array<juce::TextButton, 3> levelButtons;
    juce::Label effectsLabel;
    std::array<EffectRow, appearance::animationEffectCount> rows;
    juce::Label loadLabel;
    LoadBar loadBar;
    juce::Label loadText;
    juce::ToggleButton followSystem{"Follow the system's \"Reduce motion\""};
    juce::Label systemNote;
    juce::Label shortcutNote;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(AnimationsPanel)
};

} // namespace daw::ui::desktop
