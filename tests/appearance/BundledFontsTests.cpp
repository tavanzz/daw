#include "daw/appearance/BundledFonts.h"
#include "daw/appearance/ThemeLibrary.h"

#include <catch2/catch_test_macros.hpp>

#include <cstdint>
#include <map>
#include <span>

using namespace daw::appearance;

namespace
{
std::uint32_t readBigEndian(std::span<const std::byte> data, std::size_t offset, int bytes)
{
    std::uint32_t value = 0;

    for (int index = 0; index < bytes; ++index)
        value = (value << 8) |
                std::to_integer<std::uint32_t>(data[offset + static_cast<std::size_t>(index)]);

    return value;
}

// The Windows (platform 3) English names of a TrueType file, by name id: 1 family,
// 2 subfamily, 16 typographic family, 17 typographic subfamily.
std::map<std::uint32_t, juce::String> fontNames(std::span<const std::byte> data)
{
    std::map<std::uint32_t, juce::String> names;
    const auto tableCount = readBigEndian(data, 4, 2);

    for (std::size_t table = 0; table < tableCount; ++table)
    {
        const auto record = 12 + 16 * table;
        const bool isNameTable = readBigEndian(data, record, 4) == 0x6e616d65; // "name"

        if (!isNameTable)
            continue;

        const auto tableStart = readBigEndian(data, record + 8, 4);
        const auto count = readBigEndian(data, tableStart + 2, 2);
        const auto stringsStart = tableStart + readBigEndian(data, tableStart + 4, 2);

        for (std::size_t index = 0; index < count; ++index)
        {
            const auto entry = tableStart + 6 + 12 * index;
            const auto platform = readBigEndian(data, entry, 2);
            const auto language = readBigEndian(data, entry + 4, 2);
            const auto nameId = readBigEndian(data, entry + 6, 2);
            const auto length = readBigEndian(data, entry + 8, 2);
            const auto offset = stringsStart + readBigEndian(data, entry + 10, 2);

            if (platform != 3 || language != 0x409)
                continue;

            juce::String text;
            for (std::size_t character = 0; character < length; character += 2)
                text += static_cast<juce::juce_wchar>(readBigEndian(data, offset + character, 2));

            names[nameId] = text;
        }
    }

    return names;
}
} // namespace

TEST_CASE("Every font a theme names is bundled with the app", "[appearance]")
{
    for (const auto& theme : ThemeLibrary::builtIn().getThemes())
    {
        for (const auto& font : theme.fonts)
        {
            INFO(theme.name.toStdString() << ": " << font.family << " " << font.style);
            CHECK(findBundledFont(font.family, font.style) != nullptr);
        }
    }
}

TEST_CASE("Every bundled font is embedded and is the family and style it claims", "[appearance]")
{
    REQUIRE(bundledFonts().size() == 11);

    for (const auto& font : bundledFonts())
    {
        INFO(font.fileName);
        const auto data = bundledFontData(font);
        REQUIRE(data.size() > 10000);
        CHECK(readBigEndian(data, 0, 4) == 0x00010000); // TrueType outlines

        // Fonts name a non-standard weight in the typographic names, standard ones
        // in the basic family and subfamily.
        auto names = fontNames(data);
        const auto family = names.count(16) != 0 ? names[16] : names[1];
        const auto style = names.count(17) != 0 ? names[17] : names[2];
        CHECK(family == font.family);
        CHECK(style == font.style);
    }
}

TEST_CASE("Only bundled fonts are found", "[appearance]")
{
    CHECK(findBundledFont("Rajdhani", "SemiBold") != nullptr);
    CHECK(findBundledFont("Rajdhani", "Light") == nullptr);
    CHECK(findBundledFont("Comic Sans MS", "Regular") == nullptr);
}
