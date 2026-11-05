#include "daw/engine/dsp/SincInterpolator.h"

#include <cmath>
#include <numbers>

namespace daw::engine::dsp
{

const SincInterpolator& SincInterpolator::get()
{
    static const SincInterpolator interpolator;
    return interpolator;
}

SincInterpolator::SincInterpolator()
{
    constexpr double pi = std::numbers::pi;
    constexpr double cutoff = 0.92; // of the Nyquist frequency
    constexpr int half = taps / 2;

    for (int phase = 0; phase <= phases; ++phase)
    {
        const double fraction = static_cast<double>(phase) / phases;
        double sum = 0.0;
        std::array<double, taps> weights{};
        for (int tap = 0; tap < taps; ++tap)
        {
            // Tap k reads sample (whole - half + 1 + k); its distance from the position.
            const double x = static_cast<double>(tap - half + 1) - fraction;
            const double sinc = std::abs(x) < 1e-12 ? 1.0 : std::sin(pi * cutoff * x) / (pi * cutoff * x);
            const double t = (x + half) / taps; // 0..1 across the window
            const double window =
                0.42 - 0.5 * std::cos(2.0 * pi * t) + 0.08 * std::cos(4.0 * pi * t);
            weights[static_cast<std::size_t>(tap)] = cutoff * sinc * window;
            sum += weights[static_cast<std::size_t>(tap)];
        }
        // Unity gain at DC.
        for (int tap = 0; tap < taps; ++tap)
            table[static_cast<std::size_t>(phase)][static_cast<std::size_t>(tap)] =
                static_cast<float>(weights[static_cast<std::size_t>(tap)] / sum);
    }
}

float SincInterpolator::read(const float* signal, juce::int64 length, double position) const noexcept
{
    const auto whole = static_cast<juce::int64>(std::floor(position));
    const double fraction = position - static_cast<double>(whole);
    const double scaled = fraction * phases;
    const auto phase = static_cast<int>(scaled);
    const auto blend = static_cast<float>(scaled - phase);
    const auto& a = table[static_cast<std::size_t>(phase)];
    const auto& b = table[static_cast<std::size_t>(std::min(phase + 1, phases))];

    const juce::int64 first = whole - taps / 2 + 1;
    float sum = 0.0f;
    if (first >= 0 && first + taps <= length)
    {
        const float* x = signal + first;
        for (int tap = 0; tap < taps; ++tap)
            sum += x[tap] * (a[static_cast<std::size_t>(tap)] +
                             (b[static_cast<std::size_t>(tap)] - a[static_cast<std::size_t>(tap)]) * blend);
        return sum;
    }

    for (int tap = 0; tap < taps; ++tap)
    {
        const juce::int64 index = first + tap;
        if (index < 0 || index >= length)
            continue;
        sum += signal[index] * (a[static_cast<std::size_t>(tap)] +
                                (b[static_cast<std::size_t>(tap)] - a[static_cast<std::size_t>(tap)]) * blend);
    }
    return sum;
}

} // namespace daw::engine::dsp
