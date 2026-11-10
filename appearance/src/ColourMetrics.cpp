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

namespace
{
struct Lab
{
    double l = 0.0, a = 0.0, b = 0.0;
};

Lab labOf(Colour colour) noexcept
{
    const double r = linear(colour.red), g = linear(colour.green), b = linear(colour.blue);
    // sRGB to XYZ, relative to the D65 white.
    const double x = (0.4124 * r + 0.3576 * g + 0.1805 * b) / 0.95047;
    const double y = 0.2126 * r + 0.7152 * g + 0.0722 * b;
    const double z = (0.0193 * r + 0.1192 * g + 0.9505 * b) / 1.08883;
    const auto f = [](double t)
    {
        constexpr double epsilon = 216.0 / 24389.0;
        constexpr double kappa = 24389.0 / 27.0;
        return t > epsilon ? std::cbrt(t) : (kappa * t + 16.0) / 116.0;
    };
    return {116.0 * f(y) - 16.0, 500.0 * (f(x) - f(y)), 200.0 * (f(y) - f(z))};
}
} // namespace

double colourDifference(Colour first, Colour second) noexcept
{
    const auto one = labOf(first);
    const auto two = labOf(second);
    return std::sqrt((one.l - two.l) * (one.l - two.l) + (one.a - two.a) * (one.a - two.a)
                     + (one.b - two.b) * (one.b - two.b));
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
