#include "daw/model/FormatVersion.h"
#include "daw/model/NetworkProtocol.h"

#include <catch2/catch_test_macros.hpp>

TEST_CASE("Only peers with the same network protocol version are compatible", "[model]")
{
    using daw::model::isCompatibleNetworkProtocol;
    using daw::model::networkProtocolVersion;

    CHECK(networkProtocolVersion >= 1);
    CHECK(isCompatibleNetworkProtocol(networkProtocolVersion));
    CHECK_FALSE(isCompatibleNetworkProtocol(networkProtocolVersion - 1));
    CHECK_FALSE(isCompatibleNetworkProtocol(networkProtocolVersion + 1));
    CHECK_FALSE(isCompatibleNetworkProtocol(0));
}
