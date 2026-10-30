#pragma once

#include "daw/engine/AudioData.h"

#include <juce_core/juce_core.h>

#include <memory>

namespace daw::engine::io
{

/** Outcome of loadAudioFile(). */
struct LoadedAudioFile
{
    /** The decoded audio, converted to the requested sample rate; null on failure. */
    std::unique_ptr<AudioData> audio;

    /** Why loading failed, written for the user. Empty on success. */
    juce::String error;

    /** Details of the file as stored on disk (valid whenever the file could be opened). */
    juce::String formatName;
    double sourceSampleRate = 0.0;
    int sourceBitsPerSample = 0;
    int numChannels = 0;

    [[nodiscard]] bool succeeded() const noexcept { return audio != nullptr; }
};

/** Longest audio accepted, in samples per channel, both as stored in the file and
    after conversion. The whole file is kept in memory: at 48 kHz this is about 93
    minutes, about 2 GB for a stereo file. */
inline constexpr juce::int64 maximumAudioLength = juce::int64{1} << 28;

/**
    Reads a whole WAV or AIFF file into memory and converts it to targetSampleRate.

    Any thread except the audio thread: it reads from disk, allocates, and can take
    seconds for long files. Never throws for bad input: unreadable, damaged or
    unsupported files produce a LoadedAudioFile with an error message.
*/
[[nodiscard]] LoadedAudioFile loadAudioFile(const juce::File& file, double targetSampleRate);

} // namespace daw::engine::io
