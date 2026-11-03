#include "daw/appearance/ColourMetrics.h"

#include <algorithm>
#include <cmath>

namespace daw::appearance
{

namespace
{
double linear(std::uint8_t channel) noexcept
{
    const double value = channel / 255.0;
    return value <= 0.04045 ? value / 12.92 : std::pow((value + 0.055) / 1.055, 2.4);
}
} // namespace

double relativeLuminance(Colour colour) noexcept
{
    return 0.2126 * linear(colour.red) + 0.7152 * linear(colour.green) +
           0.0722 * linear(colour.blue);
}

double contrastRatio(Colour first, Colour second) noexcept
{
    const double a = relativeLuminance(first);
    const double b = relativeLuminance(second);
    return (std::max(a, b) + 0.05) / (std::min(a, b) + 0.05);
}

double lightness(Colour colour) noexcept
{
    // Y relative to the D65 white is the relative luminance; CIELAB's cube-root
    // response, with its linear part near black.
    const double y = relativeLuminance(colour);
    constexpr double epsilon = 216.0 / 24389.0;
    constexpr double kappa = 24389.0 / 27.0;
    return y > epsilon ? 116.0 * std::cbrt(y) - 16.0 : kappa * y;
}

} // namespace daw::appearance
