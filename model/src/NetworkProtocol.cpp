#include "daw/model/NetworkProtocol.h"

namespace daw::model
{

bool isCompatibleNetworkProtocol(int peerVersion) noexcept
{
    return peerVersion == networkProtocolVersion;
}

} // namespace daw::model
