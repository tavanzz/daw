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

} // namespace

class Resampling::Kernel
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

juce::int64 resampledLength(juce::int64 inputLength, double inputRate, double outputRate) noexcept
{
    return static_cast<juce::int64>(
        std::llround(static_cast<double>(inputLength) * outputRate / inputRate));
}

Resampling::Resampling(double fromRate, double toRate)
    : inputRate(fromRate)
    , outputRate(toRate)
    , step(fromRate / toRate)
    , kernel(std::make_unique<const Kernel>(std::min(1.0, toRate / fromRate)))
{
    jassert(fromRate > 0.0 && toRate > 0.0);
}

Resampling::~Resampling() = default;

juce::int64 Resampling::firstInputFor(juce::int64 outputStart) const noexcept
{
    return static_cast<juce::int64>(std::floor(static_cast<double>(outputStart) * step)) -
           kernel->tapsBeforeCentre();
}

juce::int64 Resampling::endInputFor(juce::int64 outputStart, int count) const noexcept
{
    return firstInputFor(outputStart + std::max(0, count - 1)) + kernel->numTaps();
}

void Resampling::resampleRange(const float* const* input, int numChannels,
                               juce::int64 inputStart, juce::int64 inputCount,
                               juce::int64 inputLength, juce::int64 outputStart, int count,
                               float* const* output) const
{
    const int numTaps = kernel->numTaps();
    std::vector<float> coefficients(static_cast<std::size_t>(numTaps));

    for (int i = 0; i < count; ++i)
    {
        const double position = static_cast<double>(outputStart + i) * step;
        const double base = std::floor(position);
        kernel->coefficientsFor(position - base, coefficients.data());

        const juce::int64 first = static_cast<juce::int64>(base) - kernel->tapsBeforeCentre();
        const bool fullyInside = first >= 0 && first + numTaps <= inputLength;

        // Samples outside the signal count as silence: their taps are left out (not
        // multiplied by 0), exactly as for a whole buffer.
        const auto begin = static_cast<int>(std::max<juce::int64>(0, -first));
        const auto end =
            static_cast<int>(std::min<juce::int64>(numTaps, inputLength - first));
        jassert(fullyInside || begin < end || end <= 0);
        jassert(first + begin >= inputStart || begin >= end);
        jassert(first + end <= inputStart + inputCount || begin >= end);
        juce::ignoreUnused(inputCount);

        // Where tap 0 falls in input (it may lie before input's first sample when the
        // signal starts after it).
        const auto offset = static_cast<std::ptrdiff_t>(first - inputStart);

        for (int channel = 0; channel < numChannels; ++channel)
        {
            const float* source = input[channel];
            float sum = 0.0f;

            if (fullyInside)
            {
                const float* window = source + offset;
                for (int tap = 0; tap < numTaps; ++tap)
                    sum += coefficients[static_cast<std::size_t>(tap)] * window[tap];
            }
            else
            {
                for (int tap = begin; tap < end; ++tap)
                    sum += coefficients[static_cast<std::size_t>(tap)] * source[offset + tap];
            }

            output[channel][i] = sum;
        }
    }
}

juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& input, double inputRate,
                                  double outputRate)
{
    jassert(inputRate > 0.0 && outputRate > 0.0);

    if (juce::exactlyEqual(inputRate, outputRate))
        return input;

    const auto outputLength = resampledLength(input.getNumSamples(), inputRate, outputRate);
    jassert(outputLength <= std::numeric_limits<int>::max());

    juce::AudioBuffer<float> output(input.getNumChannels(), static_cast<int>(outputLength));
    const Resampling resampling(inputRate, outputRate);
    resampling.resampleRange(input.getArrayOfReadPointers(), input.getNumChannels(), 0,
                             input.getNumSamples(), input.getNumSamples(), 0,
                             output.getNumSamples(), output.getArrayOfWritePointers());
    return output;
}

} // namespace daw::engine::dsp
