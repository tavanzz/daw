#include "daw/engine/dsp/PanLaw.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace daw::engine::dsp
{

StereoGains panGains(double pan) noexcept
{
    pan = std::isfinite(pan) ? std::clamp(pan, -1.0, 1.0) : 0.0;

    // Both sides from sin(), so each is exactly 0 at the opposite extreme; in double,
    // the centre rounds to exactly 1.0f.
    const double quarterTurn = std::numbers::pi / 4.0;
    return {static_cast<float>(std::numbers::sqrt2 * std::sin((1.0 - pan) * quarterTurn)),
            static_cast<float>(std::numbers::sqrt2 * std::sin((1.0 + pan) * quarterTurn))};
}

} // namespace daw::engine::dsp
