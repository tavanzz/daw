#pragma once

#include "daw/engine/fx/EffectInstance.h"

#include "support/RealtimeCheck.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <cmath>
#include <map>
#include <memory>
#include <numbers>
#include <vector>

namespace daw::test
{

/**
    Runs one effect the way the mixer does, through an EffectInstance: parameters
    set in their units (reached at once on the first block, ramped afterwards),
    optional automation, blocks of a chosen size, heap operations counted.
*/
class EffectRunner
{
public:
    explicit EffectRunner(std::unique_ptr<engine::fx::Effect> effect, double rate = 48000.0,
                          int maximumBlock = 512)
        : sampleRate(rate)
        , instance(std::move(effect), setupFor(rate, maximumBlock))
    {
        scratch.prepare(maximumBlock);
        const auto& parameters = instance.getDescription().parameters;
        for (const auto& parameter : parameters)
            targets.push_back(parameter.toNormalised(parameter.defaultValue));
        automation.assign(parameters.size(), nullptr);
    }

    /** Sets a parameter (in its units). False if there is no such parameter. */
    bool set(const juce::String& id, float value)
    {
        const int index = instance.getDescription().indexOf(id);
        if (index < 0)
            return false;
        targets[static_cast<std::size_t>(index)] =
            instance.getDescription().parameters[static_cast<std::size_t>(index)].toNormalised(value);
        return true;
    }

    /** Automates a parameter: normalised values, one per sample from the next run()
        on (the lane must be long enough for everything run() is given). */
    bool automate(const juce::String& id, std::vector<float> normalised)
    {
        const int index = instance.getDescription().indexOf(id);
        if (index < 0)
            return false;
        lanes[index] = std::move(normalised);
        lanePosition = 0;
        return true;
    }

    /** Processes input (one or two channels; a mono input is used on both) in blocks
        of blockSize and returns the stereo output. */
    juce::AudioBuffer<float> run(const juce::AudioBuffer<float>& input, int blockSize = 512)
    {
        juce::AudioBuffer<float> output(2, input.getNumSamples());
        for (int channel = 0; channel < 2; ++channel)
            output.copyFrom(channel, 0, input, std::min(channel, input.getNumChannels() - 1), 0,
                            input.getNumSamples());

        engine::fx::SlotSettings settings;
        settings.parameters = targets;
        settings.enabled = enabled;

        for (int done = 0; done < output.getNumSamples();)
        {
            const int piece = std::min(blockSize, output.getNumSamples() - done);
            for (auto& [index, lane] : lanes)
                automation[static_cast<std::size_t>(index)] =
                    lane.data() + static_cast<std::ptrdiff_t>(lanePosition) + done;

            engine::fx::SlotContext context;
            context.time = time;
            context.time.position = time.position + done;
            if (!lanes.empty())
                context.automation = automation;

            float* const channels[] = {output.getWritePointer(0, done),
                                       output.getWritePointer(1, done)};
            heapOperations += heapOperationsDuring(
                [&] { instance.process(channels, piece, settings, context, scratch); });
            done += piece;
        }

        lanePosition += static_cast<std::size_t>(output.getNumSamples());
        time.position += output.getNumSamples();
        return output;
    }

    /** The values the effect got at the last sample, in their units. */
    [[nodiscard]] std::vector<float> values() const
    {
        const auto current = instance.getCurrentValues();
        return {current.begin(), current.end()};
    }

    double sampleRate;
    engine::fx::EffectInstance instance;
    engine::fx::EffectScratch scratch;
    std::vector<float> targets;
    bool enabled = true;
    engine::fx::TimeInfo time{120.0, 4, 4, 0, true};
    std::size_t heapOperations = 0;

private:
    static engine::fx::ProcessSetup setupFor(double rate, int maximumBlock)
    {
        engine::fx::ProcessSetup setup;
        setup.sampleRate = rate;
        setup.maximumBlockSize = maximumBlock;
        return setup;
    }

    std::map<int, std::vector<float>> lanes;
    std::vector<const float*> automation;
    std::size_t lanePosition = 0;
};

/** amplitude * sin(2 pi frequency n / rate), mono. */
inline juce::AudioBuffer<float> sine(double frequency, double rate, int length, double amplitude)
{
    juce::AudioBuffer<float> buffer(1, length);
    for (int n = 0; n < length; ++n)
        buffer.setSample(0, n,
                         static_cast<float>(amplitude * std::sin(2.0 * std::numbers::pi * frequency *
                                                                 n / rate)));
    return buffer;
}

/** The amplitude of the sinusoid of frequency in samples [start, start + length) of
    one channel: a least-squares fit of a sine and a cosine, exact for any window. */
inline double amplitudeOf(const juce::AudioBuffer<float>& buffer, int channel, double frequency,
                          double rate, int start, int length)
{
    double ss = 0.0, cc = 0.0, sc = 0.0, ys = 0.0, yc = 0.0;
    for (int n = start; n < start + length; ++n)
    {
        const double phase = 2.0 * std::numbers::pi * frequency * n / rate;
        const double s = std::sin(phase);
        const double c = std::cos(phase);
        const double y = buffer.getSample(channel, n);
        ss += s * s;
        cc += c * c;
        sc += s * c;
        ys += y * s;
        yc += y * c;
    }
    const double determinant = ss * cc - sc * sc;
    const double a = (ys * cc - yc * sc) / determinant;
    const double b = (yc * ss - ys * sc) / determinant;
    return std::sqrt(a * a + b * b);
}

inline double decibels(double gain)
{
    return 20.0 * std::log10(std::max(gain, 1.0e-30));
}

} // namespace daw::test
