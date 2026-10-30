#pragma once

namespace daw::engine::dsp
{

struct StereoGains
{
    float left = 1.0f;
    float right = 1.0f;
};

/**
    Constant-power pan law, compensated so that the centre is 0 dB: a centred track
    plays at unity on both channels, and moving it keeps left² + right² = 2.

    pan -1 (hard left) gives left +3 dB (√2), right silent; pan 1 the reverse. On a
    stereo track each gain applies to its own channel (it works as a balance).
*/
[[nodiscard]] StereoGains panGains(double pan) noexcept;

} // namespace daw::engine::dsp
