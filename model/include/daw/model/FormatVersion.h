#pragma once

namespace daw::model
{

/** Version of the project file format written by this build.

    Bump it whenever the on-disk format changes, and add a migration from the
    previous version so that older projects keep loading. */
inline constexpr int currentFormatVersion = 3;

/** True if this build can read a project saved with the given format version.
    Files written by newer builds are rejected rather than half-read. */
[[nodiscard]] bool canReadFormatVersion(int version) noexcept;

} // namespace daw::model
