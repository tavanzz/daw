#include "daw/engine/rt/AudioFifo.h"

#include <algorithm>

namespace daw::engine::rt
{

AudioFifo::AudioFifo(int numChannels, int capacity)
    : storage(std::max(1, numChannels), std::max(1, capacity))
{
    storage.clear();
}

bool AudioFifo::push(const float* const* channels, int numSamples) noexcept
{
    const auto capacity = static_cast<std::size_t>(getCapacity());
    const auto writePosition = written.load(std::memory_order_relaxed);
    const auto readPosition = read.load(std::memory_order_acquire);

    if (numSamples < 0 ||
        writePosition - readPosition + static_cast<std::size_t>(numSamples) > capacity)
        return false;

    const auto start = static_cast<int>(writePosition % capacity);
    const int first = std::min(numSamples, getCapacity() - start);

    for (int channel = 0; channel < getNumChannels(); ++channel)
    {
        float* destination = storage.getWritePointer(channel);
        const float* source = channels[channel];

        if (source != nullptr)
        {
            juce::FloatVectorOperations::copy(destination + start, source, first);
            juce::FloatVectorOperations::copy(destination, source + first, numSamples - first);
        }
        else
        {
            juce::FloatVectorOperations::clear(destination + start, first);
            juce::FloatVectorOperations::clear(destination, numSamples - first);
        }
    }

    written.store(writePosition + static_cast<std::size_t>(numSamples), std::memory_order_release);
    return true;
}

int AudioFifo::pop(float* const* destination, int maxSamples) noexcept
{
    const auto capacity = static_cast<std::size_t>(getCapacity());
    const auto readPosition = read.load(std::memory_order_relaxed);
    const auto writePosition = written.load(std::memory_order_acquire);

    const int count = std::min(maxSamples, static_cast<int>(writePosition - readPosition));
    if (count <= 0)
        return 0;

    const auto start = static_cast<int>(readPosition % capacity);
    const int first = std::min(count, getCapacity() - start);

    for (int channel = 0; channel < getNumChannels(); ++channel)
    {
        const float* source = storage.getReadPointer(channel);
        juce::FloatVectorOperations::copy(destination[channel], source + start, first);
        juce::FloatVectorOperations::copy(destination[channel] + first, source, count - first);
    }

    read.store(readPosition + static_cast<std::size_t>(count), std::memory_order_release);
    return count;
}

int AudioFifo::getNumReady() const noexcept
{
    return static_cast<int>(written.load(std::memory_order_acquire) -
                            read.load(std::memory_order_acquire));
}

} // namespace daw::engine::rt
