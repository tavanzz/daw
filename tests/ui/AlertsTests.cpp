#include "daw/ui/desktop/Alerts.h"

#include <catch2/catch_test_macros.hpp>

using daw::ui::desktop::alertButtonIndex;

TEST_CASE("Themed alerts report the button index native ones would", "[ui]")
{
    // An alert window's button X returns (X + 1) % N; Escape returns 0.
    CHECK(alertButtonIndex(1, 3) == 0); // Save
    CHECK(alertButtonIndex(2, 3) == 1); // Don't Save
    CHECK(alertButtonIndex(0, 3) == 2); // Cancel, or Escape

    CHECK(alertButtonIndex(1, 2) == 0);
    CHECK(alertButtonIndex(0, 2) == 1);

    CHECK(alertButtonIndex(0, 1) == 0); // OK
    CHECK(alertButtonIndex(0, 0) == 0);
}
