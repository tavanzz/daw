#pragma once

#include "daw/model/Clip.h"
#include "daw/model/Grid.h"

#include <optional>

namespace daw::controller
{

/** The shortest clip trimming can leave: 1 ms. */
inline constexpr model::Ticks minimumTrimmedLength = model::ticksPerSecond / 1000;

/** Snaps positions to a grid, or leaves them alone when snapping is off. */
class Snapper
{
public:
    /** No snapping. */
    Snapper() = default;
    explicit Snapper(const model::Grid& gridToSnapTo);

    [[nodiscard]] model::Ticks operator()(model::Ticks position) const noexcept;
    [[nodiscard]] bool isSnapping() const noexcept { return grid.has_value(); }

private:
    std::optional<model::Grid> grid;
};

/**
    What dragging a clip does to it. The UI turns pointer positions into ticks with
    TimelineViewport and calls these; they hold the editing rules for every UI.
*/

/** A clip moved by pointerDelta from originalPosition: its start snaps to the
    grid, and never goes before 0. */
[[nodiscard]] model::Ticks movedPosition(model::Ticks originalPosition, model::Ticks pointerDelta,
                                         const Snapper& snap) noexcept;

/** The start edge dragged to newStart, the end staying put. The start snaps; it
    cannot go before 0, before the start of the file, or closer to the end than
    minimumTrimmedLength. Clips of unknown length are returned unchanged. */
[[nodiscard]] model::ClipBounds trimmedStart(const model::ClipBounds& original,
                                             model::Ticks newStart, const Snapper& snap) noexcept;

/** The end edge dragged to newEnd. The end snaps; it cannot go past the end of the
    file (when its length is known) or closer to the start than minimumTrimmedLength.
    Clips of unknown length are returned unchanged. */
[[nodiscard]] model::ClipBounds trimmedEnd(const model::ClipBounds& original, model::Ticks newEnd,
                                           std::optional<model::Ticks> fileLength,
                                           const Snapper& snap) noexcept;

} // namespace daw::controller
