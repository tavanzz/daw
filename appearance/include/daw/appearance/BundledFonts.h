#pragma once

#include <juce_core/juce_core.h>

#include <cstddef>
#include <span>

namespace daw::appearance
{

/** A font file built into the app (appearance/fonts, all under the SIL Open Font
    License; see docs/THIRD_PARTY.md). Nothing is downloaded at run time. */
struct BundledFont
{
    const char* family;   ///< as the themes name it, e.g. "Chakra Petch"
    const char* style;    ///< e.g. "SemiBold"
    const char* fileName; ///< e.g. "ChakraPetch-SemiBold.ttf"
};

/** Every bundled font. */
[[nodiscard]] std::span<const BundledFont> bundledFonts() noexcept;

/** The bundled font of this family and style, or nullptr. */
[[nodiscard]] const BundledFont* findBundledFont(const juce::String& family,
                                                 const juce::String& style) noexcept;

/** The font file as embedded in the app (a TrueType file image). */
[[nodiscard]] std::span<const std::byte> bundledFontData(const BundledFont& font) noexcept;

} // namespace daw::appearance
