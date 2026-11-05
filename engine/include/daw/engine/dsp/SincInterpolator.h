#pragma once

#include <juce_core/juce_core.h>

#include <array>

namespace daw::engine::dsp
{

/**
    Reads a signal between its samples with an 8-point windowed sinc (a Blackman
    window, cut off a little below the Nyquist frequency), from a table of 1024
    fractional positions interpolated linearly: flat to about 18 kHz at 44.1 kHz and
    far quieter images than linear or cubic interpolation. Outside the signal it is
    silence.

    The table is built once for the app; reading is real-time safe.
*/
class SincInterpolator
{
public:
    static constexpr int taps = 8;
    static constexpr int phases = 1024;

    static const SincInterpolator& get();

    /** The signal (length samples) at position. */
    [[nodiscard]] float read(const float* signal, juce::int64 length, double position) const noexcept;

private:
    SincInterpolator();

    std::array<std::array<float, taps>, phases + 1> table{};
};

} // namespace daw::engine::dsp
