#pragma once

#include "daw/engine/AudioData.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <memory>

namespace daw::engine
{

/**
    The audio a clip plays from: samples at the rate the engine renders at.

    The mixer reads clips only through this interface, so the whole file does not
    have to be in memory: a streaming source (io::StreamingAudioSource) reads from
    blocks that a disk thread loads ahead of the playhead, and reports a dropout
    instead of blocking when the data is not there in time.

    Created and destroyed off the audio thread; never modified once shared with a
    renderer, so any number of threads may read it.
*/
class AudioSource
{
public:
    virtual ~AudioSource() = default;

    [[nodiscard]] virtual int getNumChannels() const noexcept = 0;
    [[nodiscard]] virtual juce::int64 getLength() const noexcept = 0; ///< in samples
    [[nodiscard]] virtual double getSampleRate() const noexcept = 0;

    /** Audio thread (or any rendering thread). Writes samples [start, start +
        numSamples) of one channel to destination; samples outside the source are
        silence. Returns false, having written silence where data was missing, if
        the source could not deliver in time. */
    virtual bool read(int channel, juce::int64 start, float* destination,
                      int numSamples) const noexcept = 0;

    /** Audio thread. Whether read() would deliver [start, start + numSamples) now. */
    [[nodiscard]] virtual bool isReady(juce::int64 start, juce::int64 numSamples) const noexcept
    {
        juce::ignoreUnused(start, numSamples);
        return true;
    }
};

/** A source held entirely in memory: the whole decoded file. */
class MemoryAudioSource final : public AudioSource
{
public:
    explicit MemoryAudioSource(AudioData audioData);

    [[nodiscard]] int getNumChannels() const noexcept override;
    [[nodiscard]] juce::int64 getLength() const noexcept override;
    [[nodiscard]] double getSampleRate() const noexcept override;

    bool read(int channel, juce::int64 start, float* destination,
              int numSamples) const noexcept override;

    [[nodiscard]] const juce::AudioBuffer<float>& getSamples() const noexcept
    {
        return audio.samples;
    }

private:
    const AudioData audio;
};

} // namespace daw::engine
