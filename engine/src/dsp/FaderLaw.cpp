#include "daw/engine/dsp/FaderLaw.h"

#include <algorithm>
#include <cmath>

namespace daw::engine::dsp
{

float faderGain(float position) noexcept
{
    if (!(position > 0.0f))
        return 0.0f;

    const double decibels =
        faderFloorDecibels +
        (faderTopDecibels - faderFloorDecibels) * std::min(1.0, static_cast<double>(position));
    return static_cast<float>(std::pow(10.0, decibels / 20.0));
}

float faderPosition(double gain) noexcept
{
    if (!(gain > 0.0))
        return 0.0f;

    const double decibels = 20.0 * std::log10(gain);
    const double position =
        (decibels - faderFloorDecibels) / (faderTopDecibels - faderFloorDecibels);
    return static_cast<float>(std::clamp(position, 0.0, 1.0));
}

} // namespace daw::engine::dsp
