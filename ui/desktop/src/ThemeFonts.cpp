#include "daw/ui/desktop/ThemeFonts.h"

namespace daw::ui::desktop
{

ThemeFonts::ThemeFonts()
{
    for (const auto& font : appearance::bundledFonts())
    {
        const auto data = appearance::bundledFontData(font);

        if (auto typeface = juce::Typeface::createSystemTypefaceFor(data.data(), data.size()))
            typefaces.push_back({&font, std::move(typeface)});
        else
            jassertfalse; // embedded but unreadable: the tests check every file
    }
}

juce::Typeface::Ptr ThemeFonts::find(const appearance::FontSpec& spec) const
{
    for (const auto& loaded : typefaces)
        if (spec.family == loaded.font->family && spec.style == loaded.font->style)
            return loaded.typeface;

    return {};
}

juce::Font ThemeFonts::font(const appearance::FontSpec& spec) const
{
    return font(spec, spec.size);
}

juce::Font ThemeFonts::font(const appearance::FontSpec& spec, float height) const
{
    auto options = juce::FontOptions().withHeight(height).withKerningFactor(spec.tracking);

    if (auto typeface = find(spec))
        return juce::Font(options.withTypeface(typeface));

    const bool bold = spec.style.containsIgnoreCase("bold");
    return juce::Font(options.withStyle(bold ? "Bold" : "Regular"));
}

bool ThemeFonts::isLoaded(const appearance::FontSpec& spec) const
{
    return find(spec) != nullptr;
}

juce::String ThemeFonts::textFor(const appearance::FontSpec& spec, const juce::String& text)
{
    return spec.uppercase ? text.toUpperCase() : text;
}

} // namespace daw::ui::desktop
