#include "daw/engine/dsp/GainRamp.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>

namespace daw::engine::dsp
{

void GainRamp::reset(float value) noexcept
{
    start = value;
    end = value;
    length = 0;
    position = 0;
}

void GainRamp::setTarget(float target, int rampLength) noexcept
{
    if (juce::exactlyEqual(target, end))
        return;

    if (rampLength < 1)
    {
        reset(target);
        return;
    }

    start = getCurrent();
    end = target;
    length = rampLength;
    position = 0;
}

float GainRamp::getCurrent() const noexcept
{
    return position < length ? gainAt(position) : end;
}

float GainRamp::gainAt(int step) const noexcept
{
    if (step >= length)
        return end;

    return start + (end - start) * (static_cast<float>(step) / static_cast<float>(length));
}

void GainRamp::apply(float* const* channels, int numChannels, int numSamples) noexcept
{
    int done = 0;

    if (position < length)
    {
        const int ramped = std::min(numSamples, length - position);
        for (int i = 0; i < ramped; ++i)
        {
            const float gain = gainAt(position + i + 1);
            for (int channel = 0; channel < numChannels; ++channel)
                channels[channel][i] *= gain;
        }

        position += ramped;
        done = ramped;
    }

    const int steady = numSamples - done;
    if (steady <= 0 || juce::exactlyEqual(end, 1.0f))
        return;

    for (int channel = 0; channel < numChannels; ++channel)
    {
        if (juce::exactlyEqual(end, 0.0f))
            juce::FloatVectorOperations::clear(channels[channel] + done, steady);
        else
            juce::FloatVectorOperations::multiply(channels[channel] + done, end, steady);
    }
}

void GainRamp::skip(int numSamples) noexcept
{
    position = std::min(length, position + std::max(0, numSamples));
}

} // namespace daw::engine::dsp
