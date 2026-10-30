#pragma once

#include <juce_audio_basics/juce_audio_basics.h>
#include <juce_core/juce_core.h>

namespace daw::test
{

/** A fresh, empty directory that is deleted with everything in it at the end of the scope. */
class TemporaryDirectory
{
public:
    TemporaryDirectory();
    ~TemporaryDirectory();

    TemporaryDirectory(const TemporaryDirectory&) = delete;
    TemporaryDirectory& operator=(const TemporaryDirectory&) = delete;

    [[nodiscard]] juce::File file(const juce::String& name) const { return directory.getChildFile(name); }

private:
    juce::File directory;
};

enum class AudioFileType
{
    wav,
    aiff,
    flac
};

/** Writes audio to file in the given format; true on success. floatingPoint selects
    IEEE float samples (WAV, 32 bits only). */
bool writeAudioFile(const juce::File& file, AudioFileType type, const juce::AudioBuffer<float>& audio,
                    double sampleRate, int bitsPerSample, bool floatingPoint = false);

} // namespace daw::test
