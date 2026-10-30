#include "daw/engine/dsp/Resampler.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>
#include <vector>

namespace daw::engine::dsp
{

namespace
{

// Filter design, see the response documented in Resampler.h. Kaiser's formulas:
// beta from the stopband attenuation, filter length from attenuation and
// transition width.
constexpr double stopbandAttenuationDb = 100.0;
constexpr double transitionWidth = 0.05; // fraction of the lower sample rate
constexpr double kaiserBeta = 0.1102 * (stopbandAttenuationDb - 8.7);

// The kernel is tabulated at this many fractional positions per input sample and
// linearly interpolated in between; the interpolation error stays below the
// stopband level.
constexpr int phasesPerSample = 2048;

double besselI0(double x)
{
    // Power series of the modified Bessel function of the first kind, order 0.
    const double halfX = x / 2.0;
    double sum = 1.0;
    double term = 1.0;

    for (int k = 1; k < 100 && term > sum * 1.0e-17; ++k)
    {
        const double factor = halfX / k;
        term *= factor * factor;
        sum += term;
    }

    return sum;
}

double sinc(double x)
{
    if (std::abs(x) < 1.0e-12)
        return 1.0;

    const double angle = std::numbers::pi * x;
    return std::sin(angle) / angle;
}

class Kernel
{
public:
    // scale < 1 when downsampling: the cutoff follows the output's Nyquist frequency
    // and the kernel gets proportionally longer (in input samples).
    explicit Kernel(double scale)
    {
        const double lengthAtLowerRate =
            (stopbandAttenuationDb - 8.0) / (2.285 * 2.0 * std::numbers::pi * transitionWidth);
        halfWidth = 0.5 * lengthAtLowerRate / scale;
        halfTaps = static_cast<int>(std::ceil(halfWidth));

        // Centre of the transition band, so that the stopband starts exactly at the
        // lower Nyquist frequency. In cycles per input sample.
        const double cutoff = (0.5 - transitionWidth / 2.0) * scale;
        const double windowNormalisation = 1.0 / besselI0(kaiserBeta);

        const auto taps = static_cast<std::size_t>(numTaps());
        table.resize(static_cast<std::size_t>(phasesPerSample + 1) * taps);

        for (int phase = 0; phase <= phasesPerSample; ++phase)
        {
            const double fraction = static_cast<double>(phase) / phasesPerSample;
            float* row = table.data() + static_cast<std::size_t>(phase) * taps;

            for (int tap = 0; tap < numTaps(); ++tap)
            {
                // Tap t multiplies input sample floor(x) + t - (halfTaps - 1).
                const double distance = fraction - static_cast<double>(tap - (halfTaps - 1));
                double value = 0.0;

                if (std::abs(distance) < halfWidth)
                {
                    const double u = distance / halfWidth;
                    const double window =
                        besselI0(kaiserBeta * std::sqrt(1.0 - u * u)) * windowNormalisation;
                    value = 2.0 * cutoff * sinc(2.0 * cutoff * distance) * window;
                }

                row[tap] = static_cast<float>(value);
            }
        }
    }

    [[nodiscard]] int numTaps() const noexcept { return 2 * halfTaps; }
    [[nodiscard]] int tapsBeforeCentre() const noexcept { return halfTaps - 1; }

    // Interpolated kernel for an output position whose fractional input offset is
    // fraction (0 <= fraction < 1).
    void coefficientsFor(double fraction, float* coefficients) const noexcept
    {
        const double phasePosition = fraction * phasesPerSample;
        const int phase = std::min(static_cast<int>(phasePosition), phasesPerSample - 1);
        const auto blend = static_cast<float>(phasePosition - phase);

        const auto taps = static_cast<std::size_t>(numTaps());
        const float* lower = table.data() + static_cast<std::size_t>(phase) * taps;
        const float* upper = lower + taps;

        for (std::size_t tap = 0; tap < taps; ++tap)
            coefficients[tap] = lower[tap] + blend * (upper[tap] - lower[tap]);
    }

private:
    double halfWidth = 0.0;
    int halfTaps = 0;
    std::vector<float> table;
};

} // namespace

juce::int64 resampledLength(juce::int64 inputLength, double inputRate, double outputRate) noexcept
{
    return static_cast<juce::int64>(
        std::llround(static_cast<double>(inputLength) * outputRate / inputRate));
}

juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& input, double inputRate,
                                  double outputRate)
{
    jassert(inputRate > 0.0 && outputRate > 0.0);

    if (juce::exactlyEqual(inputRate, outputRate))
        return input;

    const int numChannels = input.getNumChannels();
    const int inputLength = input.getNumSamples();
    const auto outputLength = resampledLength(inputLength, inputRate, outputRate);
    jassert(outputLength <= std::numeric_limits<int>::max());

    juce::AudioBuffer<float> output(numChannels, static_cast<int>(outputLength));

    const Kernel kernel(std::min(1.0, outputRate / inputRate));
    const int numTaps = kernel.numTaps();
    std::vector<float> coefficients(static_cast<std::size_t>(numTaps));

    const double step = inputRate / outputRate;

    for (int n = 0; n < output.getNumSamples(); ++n)
    {
        const double position = n * step;
        const double base = std::floor(position);
        kernel.coefficientsFor(position - base, coefficients.data());

        const int first = static_cast<int>(base) - kernel.tapsBeforeCentre();
        const bool fullyInside = first >= 0 && first + numTaps <= inputLength;

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float* source = input.getReadPointer(channel);
            float sum = 0.0f;

            if (fullyInside)
            {
                const float* window = source + first;
                for (int tap = 0; tap < numTaps; ++tap)
                    sum += coefficients[static_cast<std::size_t>(tap)] * window[tap];
            }
            else
            {
                // Near the ends: samples outside the input count as silence.
                const int begin = std::max(0, -first);
                const int end = std::min(numTaps, inputLength - first);
                for (int tap = begin; tap < end; ++tap)
                    sum += coefficients[static_cast<std::size_t>(tap)] * source[first + tap];
            }

            output.setSample(channel, n, sum);
        }
    }

    return output;
}

} // namespace daw::engine::dsp
