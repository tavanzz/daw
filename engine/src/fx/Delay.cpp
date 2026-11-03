#include "daw/engine/fx/Delay.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace daw::engine::fx
{

namespace
{

constexpr float lowCutOff = 20.0f;     // at or below: no low cut
constexpr float highCutOff = 20000.0f; // at or above: no high cut

// Echoes this quiet become zero, sample by sample: far below hearing, and the delay
// comes to rest (and skips its work) much sooner.
constexpr float restingLevel = 1.0e-15f;

double onePoleCoefficient(double frequency, double sampleRate) noexcept
{
    const double g = std::tan(std::numbers::pi * std::min(frequency, 0.49 * sampleRate) / sampleRate);
    return g / (1.0 + g);
}

} // namespace

const std::array<Delay::NoteValue, 18> Delay::noteValues{{
    {"1/64", 1.0 / 16.0},  {"1/32T", 1.0 / 12.0}, {"1/32", 1.0 / 8.0},  {"1/16T", 1.0 / 6.0},
    {"1/32D", 3.0 / 16.0}, {"1/16", 1.0 / 4.0},   {"1/8T", 1.0 / 3.0},  {"1/16D", 3.0 / 8.0},
    {"1/8", 1.0 / 2.0},    {"1/4T", 2.0 / 3.0},   {"1/8D", 3.0 / 4.0},  {"1/4", 1.0},
    {"1/2T", 4.0 / 3.0},   {"1/4D", 3.0 / 2.0},   {"1/2", 2.0},         {"1/1T", 8.0 / 3.0},
    {"1/2D", 3.0},         {"1/1", 4.0},
}};

Delay::Delay()
{
    description.type = typeId;
    description.name = "Delay";

    juce::StringArray names;
    for (const auto& value : noteValues)
        names.add(value.name);

    const auto make = [](const char* id, const char* name, const char* unit, float minimum,
                         float maximum, float defaultValue, ParameterScale scale) {
        ParameterInfo info;
        info.id = id;
        info.name = name;
        info.unit = unit;
        info.minimum = minimum;
        info.maximum = maximum;
        info.defaultValue = defaultValue;
        info.scale = scale;
        return info;
    };

    description.parameters = {
        switchParameter("sync", "Sync", false),
        make("time", "Time", "ms", 1.0f, 4000.0f, 350.0f, ParameterScale::logarithmic),
        choiceParameter("note", "Note", names, 8),
        make("feedback", "Feedback", "%", 0.0f, 95.0f, 35.0f, ParameterScale::linear),
        make("mix", "Mix", "%", 0.0f, 100.0f, 25.0f, ParameterScale::linear),
        switchParameter("pingPong", "Ping-pong", false),
        make("lowCut", "Low cut", "Hz", lowCutOff, 2000.0f, 100.0f, ParameterScale::logarithmic),
        make("highCut", "High cut", "Hz", 1000.0f, highCutOff, 8000.0f, ParameterScale::logarithmic),
    };
}

const EffectDescription& Delay::getDescription() const noexcept
{
    return description;
}

int Delay::delaySamples(bool synced, double milliseconds, int noteIndex, double tempo,
                        double rate) noexcept
{
    double seconds = milliseconds / 1000.0;
    if (synced)
    {
        const auto index = static_cast<std::size_t>(
            std::clamp(noteIndex, 0, static_cast<int>(noteValues.size()) - 1));
        seconds = noteValues[index].quarters * 60.0 / std::max(tempo, 1.0);
    }

    const auto samples = std::llround(std::min(seconds, maximumSeconds) * rate);
    return static_cast<int>(std::max<juce::int64>(1, samples));
}

void Delay::prepare(const ProcessSetup& setup)
{
    sampleRate = setup.sampleRate;
    capacity = static_cast<int>(std::ceil(maximumSeconds * sampleRate)) + 1;
    for (auto& line : lines)
        line.assign(static_cast<std::size_t>(capacity), 0.0f);
    fadeLength = std::max(1, static_cast<int>(std::lround(timeChangeSeconds * sampleRate)));
    lowCutFor = highCutFor = -1.0f;
    reset();
}

void Delay::reset() noexcept
{
    for (auto& line : lines)
        std::fill(line.begin(), line.end(), 0.0f);
    writePosition = 0;
    fadeDone = fadeLength;
    started = false;
    for (auto& filter : lowPassFilters)
        filter.state = 0.0;
    for (auto& filter : highPassFilters)
        filter.state = 0.0;
    silentFor = capacity;
}

float Delay::tap(int channel, int samples) const noexcept
{
    int position = writePosition - samples;
    if (position < 0)
        position += capacity;
    return lines[static_cast<std::size_t>(channel)][static_cast<std::size_t>(position)];
}

void Delay::process(const ProcessBlock& block) noexcept
{
    const auto& values = block.parameters;
    const int numChannels = std::min(block.numChannels, 2);

    // At rest with silence coming in: only time passes.
    if (silentFor >= capacity + fadeLength)
    {
        bool silence = true;
        for (int channel = 0; channel < numChannels && silence; ++channel)
            for (int i = 0; i < block.numSamples && silence; ++i)
                silence = juce::exactlyEqual(block.channels[channel][i], 0.0f);
        if (silence)
        {
            writePosition = (writePosition + block.numSamples) % capacity;
            return;
        }
    }

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float in[2] = {block.channels[0][i],
                             numChannels > 1 ? block.channels[1][i] : block.channels[0][i]};

        // Nothing in and nothing left in the line: silence out, nothing to compute.
        if (silentFor >= capacity + fadeLength && juce::exactlyEqual(in[0], 0.0f) &&
            juce::exactlyEqual(in[1], 0.0f))
        {
            writePosition = (writePosition + 1) % capacity;
            continue;
        }

        const int wanted = delaySamples(values.at(sync, i) >= 0.5f, values.at(time, i),
                                        static_cast<int>(values.at(note, i)), block.time.tempo,
                                        sampleRate);
        if (!started)
        {
            started = true;
            delay = previousDelay = wanted;
        }
        else if (wanted != delay && fadeDone >= fadeLength)
        {
            // One change at a time: a newer one waits for this crossfade.
            previousDelay = delay;
            delay = wanted;
            fadeDone = 0;
        }

        const float lowCutHz = values.at(lowCut, i);
        const float highCutHz = values.at(highCut, i);
        if (!juce::exactlyEqual(lowCutHz, lowCutFor))
        {
            lowCutFor = lowCutHz;
            lowCutCoefficient = onePoleCoefficient(lowCutHz, sampleRate);
        }
        if (!juce::exactlyEqual(highCutHz, highCutFor))
        {
            highCutFor = highCutHz;
            highCutCoefficient = onePoleCoefficient(highCutHz, sampleRate);
        }

        const float amount = std::min(values.at(feedback, i), 95.0f) / 100.0f;
        const float wetGain = values.at(mix, i) / 100.0f;
        const bool crossing = values.at(pingPong, i) >= 0.5f;

        float echo[2];
        for (int channel = 0; channel < 2; ++channel)
        {
            float read = tap(channel, delay);
            if (fadeDone < fadeLength)
            {
                const float old = tap(channel, previousDelay);
                const auto fade = static_cast<float>(fadeDone + 1) / static_cast<float>(fadeLength);
                read = old + (read - old) * fade;
            }

            double shaped = read;
            if (highCutHz < highCutOff)
                shaped = lowPassFilters[static_cast<std::size_t>(channel)].lowPass(shaped, highCutCoefficient);
            if (lowCutHz > lowCutOff)
                shaped -= highPassFilters[static_cast<std::size_t>(channel)].lowPass(shaped, lowCutCoefficient);
            echo[channel] = static_cast<float>(shaped);
        }
        if (fadeDone < fadeLength)
            ++fadeDone;

        float written[2];
        if (crossing)
        {
            written[0] = 0.5f * (in[0] + in[1]) + amount * echo[1];
            written[1] = amount * echo[0];
        }
        else
        {
            written[0] = in[0] + amount * echo[0];
            written[1] = in[1] + amount * echo[1];
        }

        for (int channel = 0; channel < 2; ++channel)
        {
            if (std::abs(written[channel]) < restingLevel)
                written[channel] = 0.0f;
            lines[static_cast<std::size_t>(channel)][static_cast<std::size_t>(writePosition)] =
                written[channel];
        }
        writePosition = (writePosition + 1) % capacity;

        const bool quiet = juce::exactlyEqual(written[0], 0.0f) && juce::exactlyEqual(written[1], 0.0f);
        silentFor = quiet ? std::min(silentFor + 1, capacity + fadeLength) : 0;

        for (int channel = 0; channel < numChannels; ++channel)
            block.channels[channel][i] = in[channel] * (1.0f - wetGain) + echo[channel] * wetGain;
    }
}

double Delay::getTailSeconds(std::span<const float> values) const
{
    const auto at = [&values](int index, float fallback) {
        return static_cast<std::size_t>(index) < values.size() ? values[static_cast<std::size_t>(index)]
                                                               : fallback;
    };
    const double seconds =
        delaySamples(at(sync, 0.0f) >= 0.5f, at(time, 350.0f), static_cast<int>(at(note, 8.0f)),
                     120.0, sampleRate) /
        sampleRate;
    const double amount = std::min(at(feedback, 35.0f), 95.0f) / 100.0;
    if (amount <= 0.0)
        return seconds;

    // Until the echoes are 100 dB down.
    return seconds * (1.0 + std::log(1.0e-5) / std::log(amount));
}

} // namespace daw::engine::fx
