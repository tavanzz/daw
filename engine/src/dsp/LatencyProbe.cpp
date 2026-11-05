#include "daw/engine/dsp/LatencyProbe.h"

#include "daw/engine/dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <limits>
#include <numbers>

namespace daw::engine::dsp
{

LatencyProbe::LatencyProbe(double sampleRate)
    : maximumLatency(static_cast<int>(std::lround(maximumLatencySeconds * sampleRate)))
    , period(burstLength + maximumLatency)
    , burst(static_cast<std::size_t>(burstLength))
{
    // A fixed pseudo-random sequence of ±amplitude: flat spectrum, one sharp peak in
    // its autocorrelation.
    juce::uint64 state = 0x2545f4914f6cdd1dull;
    for (auto& sample : burst)
    {
        state ^= state << 13;
        state ^= state >> 7;
        state ^= state << 17;
        sample = (state & 1u) != 0 ? amplitude : -amplitude;
    }

    signal.assign(static_cast<std::size_t>(numBursts * period), 0.0f);
    for (int b = 0; b < numBursts; ++b)
        std::copy(burst.begin(), burst.end(),
                  signal.begin() + static_cast<std::ptrdiff_t>(b * period));
}

LatencyProbe::Measurement LatencyProbe::analyse(const float* captured, int numSamples) const
{
    Measurement measurement;
    if (numSamples < static_cast<int>(signal.size()))
    {
        measurement.problem = "The measurement was cut short.";
        return measurement;
    }

    // Lag d of the window after a burst: sum of burst[i] * captured[start + d + i].
    const auto size = nextPowerOfTwo(static_cast<std::size_t>(period));
    Spectrum burstSpectrum(size);
    for (std::size_t i = 0; i < burst.size(); ++i)
        burstSpectrum[i] = burst[i];
    fft(burstSpectrum, false);

    double burstEnergy = 0.0;
    for (const auto sample : burst)
        burstEnergy += static_cast<double>(sample) * static_cast<double>(sample);

    double worstClarity = std::numeric_limits<double>::max();
    double weakestLevel = std::numeric_limits<double>::max();
    int invertedCount = 0;

    for (int b = 0; b < numBursts; ++b)
    {
        Spectrum window(size);
        const auto start = static_cast<std::size_t>(b * period);
        for (std::size_t i = 0; i < static_cast<std::size_t>(period); ++i)
            window[i] = captured[start + i];

        fft(window, false);
        for (std::size_t i = 0; i < size; ++i)
            window[i] *= std::conj(burstSpectrum[i]);
        fft(window, true);

        // Only lags whose whole burst lies inside the window.
        int peak = 0;
        for (int lag = 1; lag <= maximumLatency; ++lag)
            if (std::abs(window[static_cast<std::size_t>(lag)].real()) >
                std::abs(window[static_cast<std::size_t>(peak)].real()))
                peak = lag;

        double other = 0.0;
        for (int lag = 0; lag <= maximumLatency; ++lag)
            if (std::abs(lag - peak) > 2)
                other = std::max(other, std::abs(window[static_cast<std::size_t>(lag)].real()));

        const double value = window[static_cast<std::size_t>(peak)].real();
        measurement.perBurst[static_cast<std::size_t>(b)] = peak;
        worstClarity = std::min(worstClarity, other > 0.0 ? std::abs(value) / other
                                                          : std::numeric_limits<double>::max());
        weakestLevel = std::min(weakestLevel, std::abs(value) / burstEnergy);
        invertedCount += value < 0.0 ? 1 : 0;
    }

    auto sorted = measurement.perBurst;
    std::sort(sorted.begin(), sorted.end());
    measurement.latency = sorted[numBursts / 2];
    measurement.clarity = worstClarity;
    measurement.level = weakestLevel;
    measurement.inverted = invertedCount > numBursts / 2;

    constexpr double minimumClarity = 3.0;
    constexpr double minimumLevel = 0.001; // -60 dB

    if (weakestLevel < minimumLevel || worstClarity < minimumClarity)
        measurement.problem = "The test signal did not come back clearly. Check that a cable "
                              "connects the chosen output to the chosen input, and that the "
                              "input's level is up.";
    else if (sorted.back() - sorted.front() > 1)
        measurement.problem = "The bursts came back with different delays: the connection is "
                              "not a plain cable, or something else is playing.";
    else
        measurement.found = true;

    return measurement;
}

} // namespace daw::engine::dsp
