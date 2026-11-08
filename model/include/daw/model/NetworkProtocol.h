#pragma once

namespace daw::model
{

/** Version of what two copies of the app exchange over the network (Phase 9: co-op).

    Separate from the project file format (currentFormatVersion): a release can change
    one without the other. Bump it whenever what peers send each other changes; two apps
    then refuse to connect with a clear message instead of misreading each other. */
inline constexpr int networkProtocolVersion = 1;

/** True if this build can talk to a peer announcing the given protocol version. For now
    only the same version is compatible. */
[[nodiscard]] bool isCompatibleNetworkProtocol(int peerVersion) noexcept;

} // namespace daw::model
