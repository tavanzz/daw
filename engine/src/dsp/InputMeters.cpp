#include "daw/engine/dsp/InputMeters.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>

namespace daw::engine::dsp
{

void InputMeters::process(const AudioInputs& inputs, int numSamples) noexcept
{
    const int channels = std::min(inputs.numChannels, AudioInputs::maxChannels);

    for (int channel = 0; channel < channels; ++channel)
    {
        const float* samples = inputs.channel(channel);
        if (samples == nullptr || numSamples <= 0)
            continue;

        const auto range = juce::FloatVectorOperations::findMinAndMax(samples, numSamples);
        const float blockPeak = std::max(-range.getStart(), range.getEnd());
        auto& peak = peaks[static_cast<std::size_t>(channel)];

        // Atomic maximum, as in PeakLevelMeter.
        float previous = peak.load(std::memory_order_relaxed);
        while (blockPeak > previous &&
               !peak.compare_exchange_weak(previous, blockPeak, std::memory_order_relaxed))
        {
        }
    }
}

float InputMeters::takePeak(int channel) noexcept
{
    if (channel < 0 || channel >= AudioInputs::maxChannels)
        return 0.0f;

    return peaks[static_cast<std::size_t>(channel)].exchange(0.0f, std::memory_order_relaxed);
}

} // namespace daw::engine::dsp
