#pragma once

#include "daw/engine/Arrangement.h"

#include <map>

namespace daw::controller
{

/**
    Keeps what an edit took away in the arrangements for a moment, silent, so that it
    fades out instead of stopping dead: effects (switched off, at their place:
    their own 10 ms crossfade to dry), sends and buses (at gain 0: the 20 ms gain
    ramp). After keepSeconds they go for good, at the next rebuild. (Tracks that are
    gone fade out in the engine.)

    Message thread.
*/
class FadeOutKeeper
{
public:
    static constexpr double keepSeconds = 0.15;

    /** Adds to arrangement (just built from the project) what previous (the one the
        engine got last) had and arrangement has not, unless kept long enough. */
    void keep(engine::Arrangement& arrangement, const engine::Arrangement* previous,
              double nowSeconds);

    /** True once something kept has been kept long enough: rebuild to let it go. */
    [[nodiscard]] bool isDue(double nowSeconds) const;

    void clear() { since.clear(); }

private:
    [[nodiscard]] bool keeps(const void* key, double nowSeconds);

    std::map<const void*, double> since; // what is kept, since when
};

} // namespace daw::controller
