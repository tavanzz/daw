#include "daw/controller/ClipEditing.h"

#include <algorithm>

namespace daw::controller
{

Snapper::Snapper(const model::Grid& gridToSnapTo)
    : grid(gridToSnapTo)
{
}

model::Ticks Snapper::operator()(model::Ticks position) const noexcept
{
    return grid ? grid->snap(position) : position;
}

model::Ticks movedPosition(model::Ticks originalPosition, model::Ticks pointerDelta,
                           const Snapper& snap) noexcept
{
    return std::max(model::Ticks{0}, snap(originalPosition + pointerDelta));
}

model::ClipBounds trimmedStart(const model::ClipBounds& original, model::Ticks newStart,
                               const Snapper& snap) noexcept
{
    if (!original.length)
        return original;

    const model::Ticks end = original.position + *original.length;
    const model::Ticks fileStart = original.position - original.offset;
    const model::Ticks earliest = std::max(model::Ticks{0}, fileStart);
    const model::Ticks latest = end - minimumTrimmedLength;

    if (latest < earliest)
        return original;

    const model::Ticks start = std::clamp(snap(newStart), earliest, latest);
    return {start, original.offset + (start - original.position), end - start};
}

model::ClipBounds trimmedEnd(const model::ClipBounds& original, model::Ticks newEnd,
                             std::optional<model::Ticks> fileLength, const Snapper& snap) noexcept
{
    if (!original.length)
        return original;

    const model::Ticks earliest = original.position + minimumTrimmedLength;
    model::Ticks end = std::max(snap(newEnd), earliest);

    if (fileLength)
        end = std::min(end, original.position + (*fileLength - original.offset));

    return {original.position, original.offset, std::max(model::Clip::minimumLength,
                                                          end - original.position)};
}

} // namespace daw::controller
