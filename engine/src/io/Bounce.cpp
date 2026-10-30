#include "daw/engine/io/Bounce.h"

#include "daw/engine/OfflineRenderer.h"

#include <juce_audio_formats/juce_audio_formats.h>

#include <algorithm>
#include <cmath>
#include <limits>
#include <random>

namespace daw::engine::io
{

int quantise(float sample, int bitsPerSample, float ditherLsb) noexcept
{
    jassert(bitsPerSample >= 8 && bitsPerSample <= 24);

    const double fullScale = std::ldexp(1.0, bitsPerSample - 1);
    const double scaled = std::nearbyint(static_cast<double>(sample) * fullScale + ditherLsb);
    return static_cast<int>(std::clamp(scaled, -fullScale, fullScale - 1.0));
}

BounceResult bounceToWav(const Arrangement& arrangement, const BounceSettings& settings,
                         const std::function<bool(double)>& keepGoing)
{
    BounceResult outcome;
    const auto fail = [&outcome, &settings](const juce::String& reason) {
        outcome.result = juce::Result::fail("Could not export \"" + settings.file.getFileName() +
                                            "\": " + reason);
        return outcome;
    };

    const bool floatingPoint = settings.bitsPerSample == 32;
    if (settings.bitsPerSample != 16 && settings.bitsPerSample != 24 && !floatingPoint)
        return fail("only 16-bit, 24-bit and 32-bit float WAV files can be written.");
    if (settings.end <= settings.start || settings.start < 0)
        return fail("there is nothing to export in the chosen range.");
    if (!(arrangement.sampleRate > 0.0))
        return fail("the sample rate is not valid.");
    if (!settings.file.getParentDirectory().isDirectory())
        return fail("its folder does not exist.");

    const juce::TemporaryFile temporary(settings.file);
    {
        auto fileStream = std::make_unique<juce::FileOutputStream>(temporary.getFile());
        if (!fileStream->openedOk())
            return fail("the file could not be created.");

        std::unique_ptr<juce::OutputStream> stream = std::move(fileStream);
        using SampleFormat = juce::AudioFormatWriterOptions::SampleFormat;
        const auto options = juce::AudioFormatWriterOptions{}
                                 .withSampleRate(arrangement.sampleRate)
                                 .withNumChannels(2)
                                 .withBitsPerSample(settings.bitsPerSample)
                                 .withSampleFormat(floatingPoint ? SampleFormat::floatingPoint
                                                                 : SampleFormat::integral);

        juce::WavAudioFormat wav;
        const auto writer = wav.createWriterFor(stream, options);
        if (writer == nullptr)
            return fail("this format is not supported at this sample rate.");

        OfflineRenderer renderer(arrangement);
        renderer.setPosition(settings.start);

        constexpr int blockSize = OfflineRenderer::defaultBlockSize;
        juce::AudioBuffer<float> block(2, blockSize);
        juce::HeapBlock<int> integers(2 * blockSize);
        std::mt19937 random(settings.ditherSeed);
        std::uniform_real_distribution<float> uniform(0.0f, 1.0f);

        const juce::int64 total = settings.end - settings.start;
        const int bits = settings.bitsPerSample;
        const int shift = 32 - bits;

        while (outcome.samplesWritten < total)
        {
            const auto length =
                static_cast<int>(std::min<juce::int64>(blockSize, total - outcome.samplesWritten));
            block.setSize(2, length, false, false, true);
            renderer.render(block);

            for (int channel = 0; channel < 2; ++channel)
                outcome.peak = std::max(outcome.peak, block.getMagnitude(channel, 0, length));

            bool written = false;
            if (floatingPoint)
            {
                written = writer->writeFromAudioSampleBuffer(block, 0, length);
            }
            else
            {
                int* channels[] = {integers.get(), integers.get() + blockSize};
                for (int i = 0; i < length; ++i)
                {
                    for (int channel = 0; channel < 2; ++channel)
                    {
                        // TPDF: the difference of two uniform values, ±1 LSB.
                        const float ditherLsb =
                            settings.dither ? uniform(random) - uniform(random) : 0.0f;
                        const auto value = static_cast<unsigned>(
                            quantise(block.getSample(channel, i), bits, ditherLsb));
                        // The writer takes 32-bit left-justified integers.
                        channels[channel][i] = static_cast<int>(value << shift);
                    }
                }

                const int* channelList[] = {channels[0], channels[1], nullptr};
                written = writer->write(channelList, length);
            }

            if (!written)
                return fail("the file could not be written (is the disk full?).");

            outcome.samplesWritten += length;

            if (keepGoing && !keepGoing(static_cast<double>(outcome.samplesWritten) /
                                        static_cast<double>(total)))
            {
                outcome.cancelled = true;
                return outcome; // the temporary file is deleted, the target untouched
            }
        }
    }

    if (!temporary.overwriteTargetFileWithTemporary())
        return fail("the existing file could not be replaced.");

    return outcome;
}

} // namespace daw::engine::io
