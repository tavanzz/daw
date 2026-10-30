#include "support/TestAudioFiles.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <memory>

namespace daw::test
{

TemporaryDirectory::TemporaryDirectory()
    : directory(juce::File::getSpecialLocation(juce::File::tempDirectory)
                    .getNonexistentChildFile("daw-tests", ""))
{
    const auto created = directory.createDirectory();
    jassert(created.wasOk());
    juce::ignoreUnused(created);
}

TemporaryDirectory::~TemporaryDirectory()
{
    directory.deleteRecursively();
}

bool writeAudioFile(const juce::File& file, AudioFileType type, const juce::AudioBuffer<float>& audio,
                    double sampleRate, int bitsPerSample, bool floatingPoint)
{
    std::unique_ptr<juce::AudioFormat> format;
    switch (type)
    {
    case AudioFileType::wav:
        format = std::make_unique<juce::WavAudioFormat>();
        break;
    case AudioFileType::aiff:
        format = std::make_unique<juce::AiffAudioFormat>();
        break;
    case AudioFileType::flac:
        format = std::make_unique<juce::FlacAudioFormat>();
        break;
    }

    file.deleteFile();
    auto fileStream = std::make_unique<juce::FileOutputStream>(file);
    if (!fileStream->openedOk())
        return false;

    std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);

    const auto options =
        juce::AudioFormatWriterOptions{}
            .withSampleRate(sampleRate)
            .withNumChannels(audio.getNumChannels())
            .withBitsPerSample(bitsPerSample)
            .withSampleFormat(floatingPoint
                                  ? juce::AudioFormatWriterOptions::SampleFormat::floatingPoint
                                  : juce::AudioFormatWriterOptions::SampleFormat::integral);

    const auto writer = format->createWriterFor(stream, options);
    if (writer == nullptr)
        return false;

    return writer->writeFromAudioSampleBuffer(audio, 0, audio.getNumSamples());
}

} // namespace daw::test
