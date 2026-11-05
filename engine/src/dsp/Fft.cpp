#include "daw/engine/dsp/Fft.h"

#include <numbers>

namespace daw::engine::dsp
{

void fft(Spectrum& data, bool inverse)
{
    const auto n = data.size();

    for (std::size_t i = 1, j = 0; i < n; ++i)
    {
        auto bit = n >> 1;
        for (; (j & bit) != 0; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }

    std::vector<std::complex<double>> twiddles;
    for (std::size_t length = 2; length <= n; length <<= 1)
    {
        // Each twiddle computed afresh from its angle: no error builds up along the way.
        const double angle =
            (inverse ? 2.0 : -2.0) * std::numbers::pi / static_cast<double>(length);
        twiddles.resize(length / 2);
        for (std::size_t k = 0; k < length / 2; ++k)
            twiddles[k] = std::polar(1.0, angle * static_cast<double>(k));

        for (std::size_t start = 0; start < n; start += length)
        {
            for (std::size_t k = 0; k < length / 2; ++k)
            {
                const auto even = data[start + k];
                const auto odd = data[start + k + length / 2] * twiddles[k];
                data[start + k] = even + odd;
                data[start + k + length / 2] = even - odd;
            }
        }
    }

    if (inverse)
        for (auto& value : data)
            value /= static_cast<double>(n);
}

std::size_t nextPowerOfTwo(std::size_t value) noexcept
{
    std::size_t power = 1;
    while (power < value)
        power <<= 1;
    return power;
}

} // namespace daw::engine::dsp
