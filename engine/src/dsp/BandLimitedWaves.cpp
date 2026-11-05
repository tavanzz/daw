#include "daw/engine/dsp/BandLimitedWaves.h"

#include "daw/engine/dsp/Fft.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace daw::engine::dsp
{

namespace
{

// The amplitude of harmonic h of a wave, as a sum of sines.
double amplitudeOf(BandLimitedWaves::Wave wave, int h)
{
    constexpr double pi = std::numbers::pi;
    switch (wave)
    {
    case BandLimitedWaves::Wave::sine:
        return h == 1 ? 1.0 : 0.0;
    case BandLimitedWaves::Wave::triangle:
        if (h % 2 == 0)
            return 0.0;
        return 8.0 / (pi * pi) * (((h - 1) / 2) % 2 == 0 ? 1.0 : -1.0) / (h * static_cast<double>(h));
    case BandLimitedWaves::Wave::saw:
        break;
    }
    return -2.0 / (pi * h);
}

} // namespace

const BandLimitedWaves& BandLimitedWaves::get()
{
    static const BandLimitedWaves waves; // C++ makes this thread safe
    return waves;
}

BandLimitedWaves::BandLimitedWaves()
{
    for (int w = 0; w < 3; ++w)
    {
        const auto wave = static_cast<Wave>(w);
        auto& set = tables[static_cast<std::size_t>(w)];
        set.resize(numTables);

        for (int index = 0; index < numTables; ++index)
        {
            auto& table = set[static_cast<std::size_t>(index)];
            table.harmonics = harmonicsOf(index);
            table.size = static_cast<int>(
                std::max<std::size_t>(2048, nextPowerOfTwo(static_cast<std::size_t>(8 * table.harmonics))));

            // A sum of sines from its spectrum: a sine of amplitude a at harmonic h is
            // bins h and n - h of -i a n / 2 and +i a n / 2.
            const auto n = static_cast<std::size_t>(table.size);
            Spectrum spectrum(n);
            for (int h = 1; h <= table.harmonics && static_cast<std::size_t>(h) < n / 2; ++h)
            {
                const double a = amplitudeOf(wave, h) * static_cast<double>(n) / 2.0;
                spectrum[static_cast<std::size_t>(h)] = {0.0, -a};
                spectrum[n - static_cast<std::size_t>(h)] = {0.0, a};
            }
            fft(spectrum, true);

            table.samples.resize(n + 3);
            for (std::size_t i = 0; i < n; ++i)
                table.samples[i + 1] = static_cast<float>(spectrum[i].real());
            table.samples[0] = table.samples[n];
            table.samples[n + 1] = table.samples[1];
            table.samples[n + 2] = table.samples[2];
        }
    }
}

int BandLimitedWaves::harmonicsOf(int index) const noexcept
{
    return std::max(1, static_cast<int>(std::floor(std::exp2(index / static_cast<double>(tablesPerOctave)) + 1e-9)));
}

BandLimitedWaves::Selection BandLimitedWaves::select(double increment) const noexcept
{
    // Harmonics allowed: h * increment below highestFraction.
    const double allowed = highestFraction / std::max(increment, 1e-12);
    const double position = std::log2(std::max(allowed, 1.0)) * tablesPerOctave;
    const int upper = std::clamp(static_cast<int>(std::floor(position)), 0, numTables - 1);
    if (upper == numTables - 1 || upper == 0)
        return {upper, upper, 0.0f};
    return {upper - 1, upper, static_cast<float>(position - upper)};
}

float BandLimitedWaves::readTable(const Table& table, double phase) noexcept
{
    const double position = phase * table.size;
    const auto whole = static_cast<int>(position);
    const auto t = static_cast<float>(position - whole);
    const float* x = table.samples.data() + (whole & (table.size - 1)); // x[1] is sample whole

    // Cubic Hermite (Catmull-Rom) through x[0..3], between x[1] and x[2].
    const float c1 = 0.5f * (x[2] - x[0]);
    const float c2 = x[0] - 2.5f * x[1] + 2.0f * x[2] - 0.5f * x[3];
    const float c3 = 0.5f * (x[3] - x[0]) + 1.5f * (x[1] - x[2]);
    return ((c3 * t + c2) * t + c1) * t + x[1];
}

float BandLimitedWaves::read(Wave wave, double phase, const Selection& selection) const noexcept
{
    const auto& set = tables[static_cast<std::size_t>(wave)];
    if (wave == Wave::sine)
        return readTable(set[0], phase);

    const float lower = readTable(set[static_cast<std::size_t>(selection.lower)], phase);
    if (selection.lower == selection.upper)
        return lower;
    const float upper = readTable(set[static_cast<std::size_t>(selection.upper)], phase);
    return lower + (upper - lower) * selection.upperWeight;
}

} // namespace daw::engine::dsp
