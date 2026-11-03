#pragma once

#include "daw/ui/desktop/ThemeManager.h"

#include <juce_gui_basics/juce_gui_basics.h>

#include <memory>
#include <vector>

namespace daw::ui::desktop
{

/** Every theme side by side: a miniature of tracks, clips and meters drawn in that
    theme, and its fonts, shapes and meter style. Clicking one uses it; the panel itself
    is drawn in the theme in use, like every window. */
class ThemePreviewsPanel final : public juce::Component, private ThemeManager::Listener
{
public:
    explicit ThemePreviewsPanel(ThemeManager& themeManager);
    ~ThemePreviewsPanel() override;

    void paint(juce::Graphics& g) override;
    void resized() override;

    /** Paints a miniature of an arrangement in a theme (tests use it too). */
    static void paintMiniature(juce::Graphics& g, const ThemedLookAndFeel& look,
                               juce::Rectangle<float> area);

private:
    class Card;

    void themeChanged() override;

    ThemeManager& themes;
    juce::Label heading;
    juce::Label hint;
    std::vector<std::unique_ptr<Card>> cards;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemePreviewsPanel)
};

} // namespace daw::ui::desktop
