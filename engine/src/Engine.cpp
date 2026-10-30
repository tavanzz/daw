#include "daw/engine/Engine.h"

namespace daw::engine
{

void Engine::prepare(double newSampleRate, int newMaximumBlockSize)
{
    jassert(newSampleRate > 0.0);
    jassert(newMaximumBlockSize > 0);

    sampleRate = newSampleRate;
    maximumBlockSize = newMaximumBlockSize;
}

void Engine::process(juce::AudioBuffer<float>& output) noexcept
{
    output.clear();
}

} // namespace daw::engine
