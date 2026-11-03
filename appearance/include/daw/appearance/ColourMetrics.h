#pragma once

#include "daw/appearance/Colour.h"

namespace daw::appearance
{

/** Relative luminance as WCAG 2 defines it (sRGB, D65), 0 for black to 1 for white.
    Alpha is ignored: composite translucent colours first (Colour::over). */
[[nodiscard]] double relativeLuminance(Colour colour) noexcept;

/** WCAG 2 contrast ratio between two opaque colours, from 1 (same luminance) to 21. */
[[nodiscard]] double contrastRatio(Colour first, Colour second) noexcept;

/** CIELAB lightness L* (D65), 0 for black to 100 for white: how light a colour looks,
    whatever its hue. */
[[nodiscard]] double lightness(Colour colour) noexcept;

} // namespace daw::appearance
