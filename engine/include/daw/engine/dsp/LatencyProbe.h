#pragma once

#include <juce_core/juce_core.h>

#include <array>
#include <vector>

namespace daw::engine::dsp
{

/**
    Measures the real round-trip latency of an audio interface through a cable from
    one of its outputs to one of its inputs.

    The probe signal is a few bursts of pseudo-random noise, each followed by enough
    silence for the longest latency measured. Whatever came back on the input is
    cross-correlated with the burst after each one (with an FFT: off the audio
    thread); the lag of the sharpest peak is that burst's latency. Every burst must
    agree to within a sample and the peak must stand out clearly, otherwise the
    measurement fails (no cable, too much noise, the wrong input).

    The latency is in the engine's frame: from the sample index at which the engine
    played something to the index at which the device delivered it on an input, the
    quantity recordings are compensated by.
*/
class LatencyProbe
{
public:
    static constexpr int numBursts = 5;
    static constexpr int burstLength = 4096;
    static constexpr double maximumLatencySeconds = 0.5;
    static constexpr float amplitude = 0.25f; // -12 dBFS

    /** Message thread. Builds the signal (allocates). */
    explicit LatencyProbe(double sampleRate);

    /** What to play, from its first sample: the bursts and the silences after them. */
    [[nodiscard]] const std::vector<float>& getSignal() const noexcept { return signal; }

    /** The longest latency that can be measured, in samples. */
    [[nodiscard]] int getMaximumLatency() const noexcept { return maximumLatency; }

    struct Measurement
    {
        bool found = false;
        juce::int64 latency = 0;   ///< samples, the median of the bursts' latencies
        bool inverted = false;     ///< the signal came back upside down
        double clarity = 0.0;      ///< the peak over the highest other correlation value
        double level = 0.0;        ///< what came back, relative to what was played
        std::array<juce::int64, numBursts> perBurst{};
        juce::String problem;      ///< why nothing was found, for the user
    };

    /** Any thread but the audio thread. captured holds the input from the moment the
        signal's first sample was played, at least getSignal().size() samples. */
    [[nodiscard]] Measurement analyse(const float* captured, int numSamples) const;

private:
    int maximumLatency;
    int period; // a burst and its silence
    std::vector<float> burst;
    std::vector<float> signal;
};

} // namespace daw::engine::dsp
