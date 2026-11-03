#include "daw/engine/fx/Limiter.h"

#include <algorithm>
#include <cmath>

namespace daw::engine::fx
{

Limiter::Limiter()
{
    description.type = typeId;
    description.name = "Limiter";

    ParameterInfo gain;
    gain.id = "inputGain";
    gain.name = "Gain";
    gain.unit = "dB";
    gain.minimum = 0.0f;
    gain.maximum = 24.0f;

    ParameterInfo top;
    top.id = "ceiling";
    top.name = "Ceiling";
    top.unit = "dB";
    top.minimum = -24.0f;
    top.maximum = 0.0f;
    top.defaultValue = -0.3f;

    ParameterInfo recovery;
    recovery.id = "release";
    recovery.name = "Release";
    recovery.unit = "ms";
    recovery.minimum = 1.0f;
    recovery.maximum = 1000.0f;
    recovery.defaultValue = 100.0f;
    recovery.scale = ParameterScale::logarithmic;

    description.parameters = {gain, top, recovery};
}

const EffectDescription& Limiter::getDescription() const noexcept
{
    return description;
}

void Limiter::prepare(const ProcessSetup& setup)
{
    sampleRate = setup.sampleRate;
    lookahead = std::max(1, static_cast<int>(std::lround(lookaheadSeconds * sampleRate)));
    for (auto& channel : delayed)
        channel.assign(static_cast<std::size_t>(lookahead), 0.0f);
    queueGains.assign(static_cast<std::size_t>(lookahead + 1), 1.0);
    queueIndices.assign(static_cast<std::size_t>(lookahead + 1), 0);
    averaged.assign(static_cast<std::size_t>(lookahead), 1.0);
    releaseFor = -1.0f;
    reset();
}

void Limiter::reset() noexcept
{
    for (auto& channel : delayed)
        std::fill(channel.begin(), channel.end(), 0.0f);
    delayPosition = 0;
    queueHead = 0;
    queueSize = 0;
    sampleIndex = 0;
    std::fill(averaged.begin(), averaged.end(), 1.0);
    averagePosition = 0;
    sum = static_cast<double>(lookahead);
    released = 1.0;
}

void Limiter::process(const ProcessBlock& block) noexcept
{
    const auto& values = block.parameters;
    const int numChannels = std::min(block.numChannels, 2);
    const auto capacity = static_cast<int>(queueGains.size());
    double lowest = 1.0;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const double drive = std::pow(10.0, values.at(inputGain, i) / 20.0);
        const double top = std::pow(10.0, values.at(ceiling, i) / 20.0);
        const float releaseMs = values.at(release, i);
        if (!juce::exactlyEqual(releaseMs, releaseFor))
        {
            releaseFor = releaseMs;
            releaseCoefficient = std::exp(-1.0 / (releaseMs * 0.001 * sampleRate));
        }

        double in[2] = {0.0, 0.0};
        double peak = 0.0;
        for (int channel = 0; channel < numChannels; ++channel)
        {
            in[channel] = block.channels[channel][i] * drive;
            peak = std::max(peak, std::abs(in[channel]));
        }
        const double required = peak > top ? top / peak : 1.0;

        // Lowest required gain over [now - lookahead, now]. What has left the window
        // goes first, so the queue never holds more than lookahead + 1 gains.
        while (queueSize > 0 &&
               queueIndices[static_cast<std::size_t>(queueHead)] < sampleIndex - lookahead)
        {
            queueHead = (queueHead + 1) % capacity;
            --queueSize;
        }
        while (queueSize > 0 &&
               queueGains[static_cast<std::size_t>((queueHead + queueSize - 1) % capacity)] >= required)
            --queueSize;
        const auto tail = static_cast<std::size_t>((queueHead + queueSize) % capacity);
        queueGains[tail] = required;
        queueIndices[tail] = sampleIndex;
        ++queueSize;
        const double held = queueGains[static_cast<std::size_t>(queueHead)];

        // Down at once, back up at the release time.
        released = std::min(held, 1.0 - (1.0 - released) * releaseCoefficient);

        // Averaged over the lookahead: a smooth bend down to each peak.
        sum += released - averaged[static_cast<std::size_t>(averagePosition)];
        averaged[static_cast<std::size_t>(averagePosition)] = released;
        averagePosition = (averagePosition + 1) % lookahead;
        const double gain = std::min(1.0, sum / static_cast<double>(lookahead));

        for (int channel = 0; channel < numChannels; ++channel)
        {
            auto& line = delayed[channel];
            const double out = line[static_cast<std::size_t>(delayPosition)] * gain;
            line[static_cast<std::size_t>(delayPosition)] = static_cast<float>(in[channel]);

            const double magnitude = std::abs(out);
            if (magnitude > top)
                largestExcess = std::max(largestExcess, magnitude / top - 1.0);
            const auto limit = static_cast<float>(top);
            block.channels[channel][i] = std::clamp(static_cast<float>(out), -limit, limit);
        }

        delayPosition = (delayPosition + 1) % lookahead;
        ++sampleIndex;
        lowest = std::min(lowest, gain);

        // Keep the running sum from drifting: start it afresh now and then.
        if (sampleIndex % 65536 == 0)
        {
            sum = 0.0;
            for (const double value : averaged)
                sum += value;
        }
    }

    reduction.report(static_cast<float>(-20.0 * std::log10(std::max(lowest, 1.0e-10))));
}

} // namespace daw::engine::fx
