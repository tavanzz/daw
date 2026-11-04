#pragma once

#include <complex>
#include <vector>

namespace daw::engine::dsp
{

using Spectrum = std::vector<std::complex<double>>;

/** In-place radix-2 FFT; data.size() must be a power of two. The inverse is scaled by
    1 / size. Off the audio thread (it allocates its twiddles). */
void fft(Spectrum& data, bool inverse);

/** The smallest power of two not below value. */
[[nodiscard]] std::size_t nextPowerOfTwo(std::size_t value) noexcept;

} // namespace daw::engine::dsp
