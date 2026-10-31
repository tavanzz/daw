#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstddef>

namespace daw::engine::rt
{

/**
    Lock-free FIFO of multichannel audio for exactly one producer thread and one
    consumer thread: how recorded samples leave the audio thread.

    The storage is allocated once, by the constructor; push() and pop() never
    allocate, lock or block. push() takes a whole block or nothing, so a full FIFO
    loses no partial block: the producer knows exactly which samples did not fit.
*/
class AudioFifo
{
public:
    /** Message thread. capacity is in samples per channel. */
    AudioFifo(int numChannels, int capacity);

    AudioFifo(const AudioFifo&) = delete;
    AudioFifo& operator=(const AudioFifo&) = delete;

    [[nodiscard]] int getNumChannels() const noexcept { return storage.getNumChannels(); }
    [[nodiscard]] int getCapacity() const noexcept { return storage.getNumSamples(); }

    /** Producer thread. Appends numSamples samples of every channel (a null channel
        appends silence). Returns false, appending nothing, if they do not all fit. */
    [[nodiscard]] bool push(const float* const* channels, int numSamples) noexcept;

    /** Consumer thread. Moves up to maxSamples samples of every channel to
        destination, oldest first; returns how many. */
    int pop(float* const* destination, int maxSamples) noexcept;

    /** Either thread; only a snapshot while the other one works. */
    [[nodiscard]] int getNumReady() const noexcept;

private:
    juce::AudioBuffer<float> storage;

    // Both counters only ever increase; positions in storage are modulo capacity.
    std::atomic<std::size_t> written{0};
    std::atomic<std::size_t> read{0};
};

} // namespace daw::engine::rt
