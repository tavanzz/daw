#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace daw::engine::dsp
{

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
