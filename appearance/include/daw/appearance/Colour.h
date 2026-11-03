#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace daw::appearance
{

/** An sRGB colour with straight (not premultiplied) alpha, independent of any GUI
    library so that every user interface can share the themes. */
struct Colour
{
    std::uint8_t red = 0;
    std::uint8_t green = 0;
    std::uint8_t blue = 0;
    std::uint8_t alpha = 255;

    /** Parses "#RRGGBB" or "#RRGGBBAA" (either case). */
    [[nodiscard]] static std::optional<Colour> fromHex(std::string_view text) noexcept;

    /** "#RRGGBB", or "#RRGGBBAA" when not opaque. */
    [[nodiscard]] std::string toHex() const;

    /** 0xAARRGGBB, the layout juce::Colour takes. */
    [[nodiscard]] constexpr std::uint32_t toArgb() const noexcept
    {
        return (std::uint32_t{alpha} << 24) | (std::uint32_t{red} << 16) |
               (std::uint32_t{green} << 8) | std::uint32_t{blue};
    }

    [[nodiscard]] constexpr bool isOpaque() const noexcept { return alpha == 255; }

    /** Each channel (alpha too) moved from this colour towards other by amount
        (0 to 1), in sRGB values, as a designer's colour picker mixes. */
    [[nodiscard]] Colour mixedWith(Colour other, float amount) const noexcept;

    /** The same colour with alpha 0 to 1. */
    [[nodiscard]] Colour withAlpha(float newAlpha) const noexcept;

    /** What the eye sees when this colour is painted over an opaque background. */
    [[nodiscard]] Colour over(Colour background) const noexcept;

    friend constexpr bool operator==(Colour, Colour) noexcept = default;
};

} // namespace daw::appearance
