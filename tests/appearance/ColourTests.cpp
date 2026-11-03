#include "daw/appearance/Colour.h"

#include <catch2/catch_test_macros.hpp>

using daw::appearance::Colour;

TEST_CASE("Colours parse from #RRGGBB and #RRGGBBAA in either case", "[appearance]")
{
    CHECK(Colour::fromHex("#07060B") == Colour{0x07, 0x06, 0x0b, 0xff});
    CHECK(Colour::fromHex("#ff2bd6") == Colour{0xff, 0x2b, 0xd6, 0xff});
    CHECK(Colour::fromHex("#00F0FF80") == Colour{0x00, 0xf0, 0xff, 0x80});

    CHECK_FALSE(Colour::fromHex("07060B").has_value());
    CHECK_FALSE(Colour::fromHex("#07060").has_value());
    CHECK_FALSE(Colour::fromHex("#07060B0").has_value());
    CHECK_FALSE(Colour::fromHex("#0706GB").has_value());
    CHECK_FALSE(Colour::fromHex("").has_value());
}

TEST_CASE("Colours print as hex and give JUCE's ARGB layout", "[appearance]")
{
    const Colour opaque{0x9d, 0x7b, 0xff, 0xff};
    CHECK(opaque.toHex() == "#9D7BFF");
    CHECK(opaque.toArgb() == 0xff9d7bffu);

    const Colour translucent{0x00, 0xf0, 0xff, 0x24};
    CHECK(translucent.toHex() == "#00F0FF24");
    CHECK(translucent.toArgb() == 0x2400f0ffu);
    CHECK(Colour::fromHex(translucent.toHex()) == translucent);
}

TEST_CASE("Mixing moves every channel towards the other colour", "[appearance]")
{
    const Colour black{0, 0, 0, 255};
    const Colour white{255, 255, 255, 255};

    CHECK(black.mixedWith(white, 0.0f) == black);
    CHECK(black.mixedWith(white, 1.0f) == white);
    CHECK(black.mixedWith(white, 0.5f) == Colour{128, 128, 128, 255});
    CHECK(Colour{200, 100, 0, 255}.mixedWith(Colour{0, 100, 200, 55}, 0.25f) ==
          Colour{150, 100, 50, 205});

    // Amounts outside 0 to 1 are clamped rather than extrapolated.
    CHECK(black.mixedWith(white, 2.0f) == white);
    CHECK(black.mixedWith(white, -1.0f) == black);
}

TEST_CASE("A translucent colour over a background blends into an opaque one", "[appearance]")
{
    const Colour background{0, 0, 0, 255};

    CHECK(Colour{255, 255, 255, 255}.over(background) == Colour{255, 255, 255, 255});
    CHECK(Colour{255, 255, 255, 0}.over(background) == background);
    CHECK(Colour{200, 100, 50, 255}.withAlpha(0.5f).over(Colour{0, 100, 250, 255}) ==
          Colour{100, 100, 150, 255});
    CHECK(Colour{1, 2, 3, 255}.withAlpha(0.5f).alpha == 128);
}
