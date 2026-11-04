#pragma once

#include "daw/engine/dsp/Fft.h"
#include "daw/engine/fx/EffectInstance.h"
#include "daw/engine/midi/MidiEvent.h"

#include "support/RealtimeCheck.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>
#include <cmath>
#include <memory>
#include <numbers>
#include <vector>

namespace daw::test
{

/**
    Plays an instrument the way the mixer does, through an EffectInstance: parameters
    set in their units (there from the first sample), events at absolute sample
    positions, blocks of a chosen size, heap operations counted.
*/
class InstrumentRunner
{
public:
    explicit InstrumentRunner(std::unique_ptr<engine::fx::Effect> instrument, double rate = 48000.0,
                              int maximumBlock = 512)
        : sampleRate(rate)
        , effect(instrument.get())
        , instance(std::move(instrument), setupFor(rate, maximumBlock))
    {
        scratch.prepare(maximumBlock);
        const auto& parameters = instance.getDescription().parameters;
        for (const auto& parameter : parameters)
            targets.push_back(parameter.toNormalised(parameter.defaultValue));
        blockEvents.reserve(8192);
    }

    bool set(const juce::String& id, float value)
    {
        const int index = instance.getDescription().indexOf(id);
        if (index < 0)
            return false;
        targets[static_cast<std::size_t>(index)] =
            instance.getDescription().parameters[static_cast<std::size_t>(index)].toNormalised(value);
        return true;
    }

    /** Automates a parameter with normalised values, one per sample from now on. */
    void automate(const juce::String& id, std::vector<float> normalised)
    {
        automationIndex = instance.getDescription().indexOf(id);
        lane = std::move(normalised);
        lanePosition = 0;
    }

    /** Renders length samples from the current position in blocks of blockSize; the
        events' offsets are absolute sample positions. Returns the left channel. */
    std::vector<float> run(std::vector<engine::midi::Event> events, int length, int blockSize = 256)
    {
        std::stable_sort(events.begin(), events.end(), engine::midi::comesBefore);
        std::vector<float> left(static_cast<std::size_t>(length));
        std::vector<float> right(static_cast<std::size_t>(length));
        engine::fx::SlotSettings settings;
        settings.parameters = targets;
        std::vector<const float*> automation;

        heapOperations += heapOperationsDuring([&] {
            for (int done = 0; done < length;)
            {
                const int piece = std::min(blockSize, length - done);
                blockEvents.clear();
                for (auto event : events)
                    if (event.offset >= position + done && event.offset < position + done + piece)
                    {
                        event.offset -= position + done;
                        blockEvents.push_back(event);
                    }

                engine::fx::SlotContext context;
                context.events = blockEvents;
                context.time.tempo = 120.0;
                if (automationIndex >= 0)
                {
                    automationPointers.assign(static_cast<std::size_t>(automationIndex + 1), nullptr);
                    automationPointers[static_cast<std::size_t>(automationIndex)] =
                        lane.data() + lanePosition;
                    context.automation = automationPointers;
                }

                float* const channels[] = {left.data() + done, right.data() + done};
                instance.process(channels, piece, settings, context, scratch);
                done += piece;
                lanePosition += static_cast<std::size_t>(piece);
            }
        });
        position += length;
        lastRight = std::move(right);
        return left;
    }

    double sampleRate;
    engine::fx::Effect* effect;
    engine::fx::EffectInstance instance;
    engine::fx::EffectScratch scratch;
    std::vector<float> targets;
    std::vector<engine::midi::Event> blockEvents;
    std::vector<float> lastRight;
    int position = 0;
    std::size_t heapOperations = 0;

private:
    static engine::fx::ProcessSetup setupFor(double rate, int maximumBlock)
    {
        engine::fx::ProcessSetup setup;
        setup.sampleRate = rate;
        setup.maximumBlockSize = maximumBlock;
        return setup;
    }

    int automationIndex = -1;
    std::vector<float> lane;
    std::size_t lanePosition = 0;
    std::vector<const float*> automationPointers;
};

/** The frequency of a steady tone over [from, to), from its rising zero crossings
    (interpolated between samples). */
inline double measuredFrequency(const std::vector<float>& samples, std::size_t from, std::size_t to,
                                double sampleRate)
{
    double first = -1.0;
    double last = -1.0;
    int crossings = 0;
    for (std::size_t i = from + 1; i < to; ++i)
    {
        const float a = samples[i - 1];
        const float b = samples[i];
        if (a < 0.0f && b >= 0.0f)
        {
            const double at = static_cast<double>(i - 1) + a / (a - b);
            if (first < 0.0)
                first = at;
            else
                ++crossings;
            last = at;
        }
    }
    return crossings > 0 ? crossings * sampleRate / (last - first) : 0.0;
}

/** The strongest component that is not a harmonic of fundamental (or DC), relative to
    the fundamental, in dB: what aliasing (or any other spurious tone) adds. From an
    FFT of the first power of two of samples, Blackman-Harris windowed (side lobes
    below -92 dB). */
inline double strongestNonHarmonicDecibels(const std::vector<float>& samples, double fundamental,
                                           double sampleRate, std::size_t size = 65536)
{
    const double pi = std::numbers::pi;
    engine::dsp::Spectrum spectrum(size);
    for (std::size_t n = 0; n < size; ++n)
    {
        const double x = static_cast<double>(n) / static_cast<double>(size - 1);
        const double window = 0.35875 - 0.48829 * std::cos(2 * pi * x) + 0.14128 * std::cos(4 * pi * x) -
                              0.01168 * std::cos(6 * pi * x);
        spectrum[n] = {samples[n] * window, 0.0};
    }
    engine::dsp::fft(spectrum, false);

    const double binHz = sampleRate / static_cast<double>(size);
    const auto magnitudeAt = [&](std::size_t bin) { return std::abs(spectrum[bin]); };

    const auto fundamentalBin = static_cast<std::size_t>(std::lround(fundamental / binHz));
    double reference = 0.0;
    for (std::size_t bin = fundamentalBin > 3 ? fundamentalBin - 3 : 0; bin <= fundamentalBin + 3; ++bin)
        reference = std::max(reference, magnitudeAt(bin));

    constexpr double guard = 8.0; // bins around each harmonic: the window's main lobe and more
    double strongest = 0.0;
    for (std::size_t bin = static_cast<std::size_t>(guard); bin < size / 2; ++bin)
    {
        const double frequency = static_cast<double>(bin) * binHz;
        const double harmonic = std::round(frequency / fundamental);
        if (harmonic >= 1.0 && std::abs(frequency - harmonic * fundamental) <= guard * binHz)
            continue;
        strongest = std::max(strongest, magnitudeAt(bin));
    }
    return 20.0 * std::log10(std::max(strongest, 1e-30) / reference);
}

} // namespace daw::test
