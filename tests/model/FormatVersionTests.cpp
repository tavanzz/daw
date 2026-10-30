#include "daw/model/FormatVersion.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Only format versions from 1 to the current one are readable", "[model]")
{
    using daw::model::canReadFormatVersion;
    using daw::model::currentFormatVersion;

    CHECK(canReadFormatVersion(1));
    CHECK(canReadFormatVersion(currentFormatVersion));

    CHECK_FALSE(canReadFormatVersion(0));
    CHECK_FALSE(canReadFormatVersion(-1));
    CHECK_FALSE(canReadFormatVersion(currentFormatVersion + 1));
}
