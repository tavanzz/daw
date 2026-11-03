#pragma once

#include "daw/model/Clip.h"

#include <compare>
#include <cstdint>

namespace daw::controller
{

/** Whose effect chain: a track's, a bus's (by id) or the master's. */
struct ChainOwner
{
    enum class Kind : std::uint8_t
    {
        track,
        bus,
        master
    };

    Kind kind = Kind::master;
    model::ObjectId id = 0; ///< 0 for the master

    [[nodiscard]] static ChainOwner track(model::ObjectId trackId) { return {Kind::track, trackId}; }
    [[nodiscard]] static ChainOwner bus(model::ObjectId busId) { return {Kind::bus, busId}; }
    [[nodiscard]] static ChainOwner master() { return {Kind::master, 0}; }

    auto operator<=>(const ChainOwner&) const = default;
};

} // namespace daw::controller
