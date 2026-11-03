#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>
#include <vector>

namespace daw::engine::dsp
{

/**
    The conversion of one rate to another: its filter, tabulated once. Converting a
    whole buffer (resample()) and converting a stream a block at a time
    (resampleRange()) use the same one and compute every output sample the same
    way, so their results are identical to the bit.

    Immutable once built: any thread may share it.
*/
class Resampling
{
public:
    /** Not real-time safe: builds the filter table. */
    Resampling(double inputRate, double outputRate);
    ~Resampling();

    Resampling(const Resampling&) = delete;
    Resampling& operator=(const Resampling&) = delete;

    [[nodiscard]] double getInputRate() const noexcept { return inputRate; }
    [[nodiscard]] double getOutputRate() const noexcept { return outputRate; }

    /** The input samples output samples [outputStart, outputStart + count) depend
        on: [first, end). May reach before 0 or past the input's end. */
    [[nodiscard]] juce::int64 firstInputFor(juce::int64 outputStart) const noexcept;
    [[nodiscard]] juce::int64 endInputFor(juce::int64 outputStart, int count) const noexcept;

    /**
        Computes output samples [outputStart, outputStart + count) of a signal of
        inputLength samples per channel into output (numChannels channels). input
        holds input samples [inputStart, inputStart + inputCount): at least
        everything from firstInputFor() to endInputFor() that lies inside the
        signal. Samples outside the signal count as silence.

        Not real-time safe (uses a scratch buffer of the kernel's size).
    */
    void resampleRange(const float* const* input, int numChannels, juce::int64 inputStart,
                       juce::int64 inputCount, juce::int64 inputLength,
                       juce::int64 outputStart, int count, float* const* output) const;

private:
    class Kernel;

    double inputRate;
    double outputRate;
    double step;
    std::unique_ptr<const Kernel> kernel;
};

/**
    Offline sample-rate conversion with a Kaiser-windowed sinc filter.

    Frequency response, relative to the lower of the two sample rates:
    - flat (ripple well below 0.001 dB) up to 45 % of it, e.g. 19.8 kHz at 44.1 kHz;
    - about 100 dB of attenuation from 50 % of it (its Nyquist frequency) upwards,
      so neither aliasing (downsampling) nor images (upsampling) are audible.
    The tests measure better than -96 dB just above the Nyquist frequency and
    better than -90 dB of total error for tones in the passband.

    No delay is added: output sample n is the signal at input position
    n * inputRate / outputRate.

    Not real-time safe: allocates, and takes time proportional to the input length.
*/
[[nodiscard]] juce::AudioBuffer<float> resample(const juce::AudioBuffer<float>& input,
                                                double inputRate, double outputRate);

/** Number of samples per channel resample() produces for this input length:
    the input length scaled by outputRate / inputRate, rounded to the nearest integer. */
[[nodiscard]] juce::int64 resampledLength(juce::int64 inputLength, double inputRate,
                                          double outputRate) noexcept;

} // namespace daw::engine::dsp
