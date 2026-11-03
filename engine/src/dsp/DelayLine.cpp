#include "daw/engine/dsp/DelayLine.h"

#include <juce_core/juce_core.h>

#include <algorithm>

namespace daw::engine::dsp
{

void DelayLine::prepare(int numChannels, int newMaximumDelay)
{
    jassert(numChannels >= 0 && newMaximumDelay >= 0);
    maximumDelay = std::max(0, newMaximumDelay);
    delay = std::min(delay, maximumDelay);

    const int length = juce::nextPowerOfTwo(maximumDelay + 1);
    mask = length - 1;
    buffers.assign(static_cast<std::size_t>(std::max(0, numChannels)),
                   std::vector<float>(static_cast<std::size_t>(length), 0.0f));
    writePosition = 0;
}

void DelayLine::setDelay(int samples) noexcept
{
    delay = std::clamp(samples, 0, maximumDelay);
}

void DelayLine::clear() noexcept
{
    for (auto& buffer : buffers)
        std::fill(buffer.begin(), buffer.end(), 0.0f);
    writePosition = 0;
}

void DelayLine::process(float* const* channels, int numChannels, int numSamples) noexcept
{
    process(channels, channels, numChannels, numSamples);
}

void DelayLine::process(const float* const* input, float* const* output, int numChannels,
                        int numSamples) noexcept
{
    jassert(numChannels <= static_cast<int>(buffers.size()));
    numChannels = std::min(numChannels, static_cast<int>(buffers.size()));

    for (int channel = 0; channel < numChannels; ++channel)
    {
        float* const buffer = buffers[static_cast<std::size_t>(channel)].data();
        const float* in = input[channel];
        float* out = output[channel];
        int position = writePosition;

        // Written before read, so that a delay of 0 passes the sample straight on.
        for (int i = 0; i < numSamples; ++i)
        {
            buffer[position] = in[i];
            out[i] = buffer[(position - delay) & mask];
            position = (position + 1) & mask;
        }
    }

    writePosition = (writePosition + numSamples) & mask;
}

} // namespace daw::engine::dsp
