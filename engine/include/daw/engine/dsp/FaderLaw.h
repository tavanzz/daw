#pragma once

namespace daw::engine::dsp
{

/** The bottom and the top of a fader. At the bottom it is silent. */
inline constexpr double faderFloorDecibels = -60.0;
inline constexpr double faderTopDecibels = 6.0;

/**
    The gain of a fader (volume, send level) at a position from 0 to 1: even in
    decibels from faderFloorDecibels to faderTopDecibels, silent at 0. Volume
    automation stores positions, so it moves as a fader would.
*/
[[nodiscard]] float faderGain(float position) noexcept;

/** The position of a linear gain on the fader (0 for silence or anything below the
    floor, 1 for the top or above). */
[[nodiscard]] float faderPosition(double gain) noexcept;

} // namespace daw::engine::dsp
