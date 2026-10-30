#include "daw/engine/io/AudioFileLoader.h"

#include "daw/engine/dsp/Resampler.h"

#include <juce_audio_formats/juce_audio_formats.h>

namespace daw::engine::io
{

namespace
{

LoadedAudioFile failure(LoadedAudioFile result, const juce::String& message)
{
    result.audio.reset();
    result.error = message;
    return result;
}

} // namespace

LoadedAudioFile loadAudioFile(const juce::File& file, double targetSampleRate)
{
    jassert(targetSampleRate > 0.0);

    LoadedAudioFile result;
    const auto quotedName = "\"" + file.getFileName() + "\"";

    if (!file.existsAsFile())
        return failure(std::move(result), quotedName + " does not exist.");

    // Phase 1 plays WAV and AIFF only, even though JUCE could decode more formats.
    juce::AudioFormatManager formats;
    formats.registerFormat(new juce::WavAudioFormat(), true);
    formats.registerFormat(new juce::AiffAudioFormat(), false);

    const std::unique_ptr<juce::AudioFormatReader> reader(formats.createReaderFor(file));

    if (reader == nullptr)
        return failure(std::move(result), quotedName + " is not a WAV or AIFF file, or it is damaged.");

    result.formatName = reader->getFormatName();
    result.sourceSampleRate = reader->sampleRate;
    result.sourceBitsPerSample = static_cast<int>(reader->bitsPerSample);
    result.numChannels = static_cast<int>(reader->numChannels);

    if (reader->sampleRate <= 0.0 || reader->numChannels == 0)
        return failure(std::move(result), quotedName + " has an invalid audio format.");

    // JUCE reports a WAV whose data chunk was cut short (an interrupted download, say)
    // as having no samples at all.
    if (reader->lengthInSamples <= 0)
        return failure(std::move(result), quotedName + " contains no audio, or is incomplete.");

    const auto convertedLength =
        dsp::resampledLength(reader->lengthInSamples, reader->sampleRate, targetSampleRate);

    if (reader->lengthInSamples > maximumAudioLength || convertedLength > maximumAudioLength)
    {
        const auto maximumMinutes =
            static_cast<int>(static_cast<double>(maximumAudioLength) / targetSampleRate / 60.0);
        return failure(std::move(result), quotedName + " is too long: the limit is " +
                                              juce::String(maximumMinutes) + " minutes.");
    }

    juce::AudioBuffer<float> decoded(result.numChannels,
                                     static_cast<int>(reader->lengthInSamples));

    if (!reader->read(decoded.getArrayOfWritePointers(), decoded.getNumChannels(), 0,
                      decoded.getNumSamples()))
        return failure(std::move(result), quotedName + " could not be read completely.");

    auto audio = std::make_unique<AudioData>();
    audio->sampleRate = targetSampleRate;
    audio->samples = juce::exactlyEqual(reader->sampleRate, targetSampleRate)
                         ? std::move(decoded)
                         : dsp::resample(decoded, reader->sampleRate, targetSampleRate);

    result.audio = std::move(audio);
    return result;
}

} // namespace daw::engine::io
