#include "daw/appearance/Colour.h"

#include <algorithm>
#include <cmath>

namespace daw::appearance
{

namespace
{
std::optional<std::uint8_t> parseHexByte(std::string_view digits) noexcept
{
    int value = 0;

    for (const char digit : digits)
    {
        value *= 16;

        if (digit >= '0' && digit <= '9')
            value += digit - '0';
        else if (digit >= 'a' && digit <= 'f')
            value += digit - 'a' + 10;
        else if (digit >= 'A' && digit <= 'F')
            value += digit - 'A' + 10;
        else
            return std::nullopt;
    }

    return static_cast<std::uint8_t>(value);
}

std::uint8_t toByte(float value) noexcept
{
    return static_cast<std::uint8_t>(std::lround(std::clamp(value, 0.0f, 255.0f)));
}

std::uint8_t mixByte(std::uint8_t from, std::uint8_t to, float amount) noexcept
{
    return toByte(static_cast<float>(from) +
                  (static_cast<float>(to) - static_cast<float>(from)) * amount);
}
} // namespace

std::optional<Colour> Colour::fromHex(std::string_view text) noexcept
{
    if (text.empty() || text.front() != '#' || (text.size() != 7 && text.size() != 9))
        return std::nullopt;

    const auto red = parseHexByte(text.substr(1, 2));
    const auto green = parseHexByte(text.substr(3, 2));
    const auto blue = parseHexByte(text.substr(5, 2));
    const auto alpha = text.size() == 9 ? parseHexByte(text.substr(7, 2)) : std::uint8_t{255};

    if (!red || !green || !blue || !alpha)
        return std::nullopt;

    return Colour{*red, *green, *blue, *alpha};
}

std::string Colour::toHex() const
{
    static constexpr char digits[] = "0123456789ABCDEF";
    std::string text = "#";

    const auto append = [&](std::uint8_t value)
    {
        text += digits[value >> 4];
        text += digits[value & 0x0f];
    };

    append(red);
    append(green);
    append(blue);

    if (!isOpaque())
        append(alpha);

    return text;
}

Colour Colour::mixedWith(Colour other, float amount) const noexcept
{
    amount = std::clamp(amount, 0.0f, 1.0f);
    return {mixByte(red, other.red, amount), mixByte(green, other.green, amount),
            mixByte(blue, other.blue, amount), mixByte(alpha, other.alpha, amount)};
}

Colour Colour::withAlpha(float newAlpha) const noexcept
{
    return {red, green, blue, toByte(std::clamp(newAlpha, 0.0f, 1.0f) * 255.0f)};
}

Colour Colour::over(Colour background) const noexcept
{
    const float opacity = static_cast<float>(alpha) / 255.0f;
    const auto blend = [opacity](std::uint8_t front, std::uint8_t back)
    {
        return toByte(static_cast<float>(front) * opacity +
                      static_cast<float>(back) * (1.0f - opacity));
    };

    return {blend(red, background.red), blend(green, background.green),
            blend(blue, background.blue), 255};
}

} // namespace daw::appearance
