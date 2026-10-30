#include "daw/engine/dsp/PeakLevelMeter.h"

#include <algorithm>

namespace daw::engine::dsp
{

void PeakLevelMeter::process(const juce::AudioBuffer<float>& buffer, int startSample,
                             int numSamples) noexcept
{
    const int channels = std::min(buffer.getNumChannels(), maxChannels);

    for (int channel = 0; channel < channels; ++channel)
    {
        const float blockPeak = buffer.getMagnitude(channel, startSample, numSamples);
        auto& peak = peaks[static_cast<std::size_t>(channel)];

        // Atomic maximum: if the reader resets the value in between, the loop
        // simply retries against the fresh value.
        float previous = peak.load(std::memory_order_relaxed);
        while (blockPeak > previous &&
               !peak.compare_exchange_weak(previous, blockPeak, std::memory_order_relaxed))
        {
        }
    }
}

float PeakLevelMeter::takePeak(int channel) noexcept
{
    if (channel < 0 || channel >= maxChannels)
        return 0.0f;

    return peaks[static_cast<std::size_t>(channel)].exchange(0.0f, std::memory_order_relaxed);
}

} // namespace daw::engine::dsp
