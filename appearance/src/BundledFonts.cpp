#include "daw/appearance/BundledFonts.h"

#include "AppearanceResources.h"

#include <array>

namespace daw::appearance
{

namespace
{
// Two weights at most per family, the ones the themes use; adding a font here also
// means adding its file to appearance/CMakeLists.txt.
constexpr std::array<BundledFont, 11> fonts{{
    {"Chakra Petch", "Regular", "ChakraPetch-Regular.ttf"},
    {"Chakra Petch", "SemiBold", "ChakraPetch-SemiBold.ttf"},
    {"JetBrains Mono", "Regular", "JetBrainsMono-Regular.ttf"},
    {"JetBrains Mono", "Bold", "JetBrainsMono-Bold.ttf"},
    {"Space Grotesk", "Medium", "SpaceGrotesk-Medium.ttf"},
    {"Space Grotesk", "Bold", "SpaceGrotesk-Bold.ttf"},
    {"IBM Plex Mono", "Regular", "IBMPlexMono-Regular.ttf"},
    {"IBM Plex Mono", "SemiBold", "IBMPlexMono-SemiBold.ttf"},
    {"Michroma", "Regular", "Michroma-Regular.ttf"},
    {"Rajdhani", "Medium", "Rajdhani-Medium.ttf"},
    {"Rajdhani", "SemiBold", "Rajdhani-SemiBold.ttf"},
}};
} // namespace

std::span<const BundledFont> bundledFonts() noexcept
{
    return fonts;
}

const BundledFont* findBundledFont(const juce::String& family, const juce::String& style) noexcept
{
    for (const auto& font : fonts)
        if (family == font.family && style == font.style)
            return &font;

    return nullptr;
}

std::span<const std::byte> bundledFontData(const BundledFont& font) noexcept
{
    namespace resources = daw::appearance::resources;

    for (int index = 0; index < resources::namedResourceListSize; ++index)
    {
        const auto* resourceName = resources::namedResourceList[index];

        if (juce::String(font.fileName) ==
            resources::getNamedResourceOriginalFilename(resourceName))
        {
            int size = 0;
            const auto* data = resources::getNamedResource(resourceName, size);
            return {reinterpret_cast<const std::byte*>(data), static_cast<std::size_t>(size)};
        }
    }

    return {};
}

} // namespace daw::appearance
