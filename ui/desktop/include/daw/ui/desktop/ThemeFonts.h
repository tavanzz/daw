#pragma once

#include "daw/appearance/BundledFonts.h"
#include "daw/appearance/Theme.h"

#include <juce_graphics/juce_graphics.h>

#include <vector>

namespace daw::ui::desktop
{

/** The fonts built into the app as JUCE typefaces, loaded once, and the font for a
    theme's font role. Message thread. */
class ThemeFonts final
{
public:
    /** Loads every bundled font (appearance::bundledFonts()). */
    ThemeFonts();

    /** The font for this spec at its own size, or at another height. A family the app
        does not bundle (or that failed to load) falls back to the default sans-serif
        face, bold for semibold and heavier styles. */
    [[nodiscard]] juce::Font font(const appearance::FontSpec& spec) const;
    [[nodiscard]] juce::Font font(const appearance::FontSpec& spec, float height) const;

    /** True if the spec's family and style are bundled and loaded. */
    [[nodiscard]] bool isLoaded(const appearance::FontSpec& spec) const;

    /** Text as the spec shows it: in capitals for roles that ask for them. */
    [[nodiscard]] static juce::String textFor(const appearance::FontSpec& spec,
                                              const juce::String& text);

private:
    struct LoadedTypeface
    {
        const appearance::BundledFont* font = nullptr;
        juce::Typeface::Ptr typeface;
    };

    [[nodiscard]] juce::Typeface::Ptr find(const appearance::FontSpec& spec) const;

    std::vector<LoadedTypeface> typefaces;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(ThemeFonts)
};

} // namespace daw::ui::desktop
