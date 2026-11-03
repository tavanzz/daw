#include "daw/engine/fx/Reverb.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace daw::engine::fx
{

namespace
{

// Line lengths at 48 kHz and the largest size, mutually prime: about 30 to 93 ms.
constexpr std::array<int, Reverb::numLines> baseLengths{1433, 1601, 1867, 2053, 2251, 2399,
                                                        2617, 2797, 3011, 3203, 3407, 3593,
                                                        3803, 4027, 4253, 4451};
constexpr std::array<int, 4> diffuserLengths{142, 107, 379, 277};
constexpr double diffusion = 0.6;
constexpr double modulationDepth = 12.0; // samples at 48 kHz
constexpr double glidePerSample = 0.125;
constexpr float lowCutOff = 20.0f;
constexpr float highCutOff = 20000.0f;

// What is written into the network this quiet becomes zero, sample by sample: far
// below hearing, and the reverb comes to rest seconds sooner (and skips its work).
constexpr double restingLevel = 1.0e-15;

double sizeFactor(double percent) noexcept
{
    return 0.3 + 0.7 * std::clamp(percent, 0.0, 100.0) / 100.0;
}

double onePoleCoefficient(double frequency, double sampleRate) noexcept
{
    const double g = std::tan(std::numbers::pi * std::min(frequency, 0.49 * sampleRate) / sampleRate);
    return g / (1.0 + g);
}

// The trapezoidal one-pole low-pass, state in place.
double lowPass(double x, double coefficient, double& state) noexcept
{
    const double v = (x - state) * coefficient;
    const double low = v + state;
    state = low + v;
    return low;
}

// In place, scaled to keep energy: the Hadamard matrix of order 16 over 4.
void hadamard(std::array<double, Reverb::numLines>& x) noexcept
{
    for (int width = 1; width < Reverb::numLines; width *= 2)
        for (int i = 0; i < Reverb::numLines; i += 2 * width)
            for (int j = i; j < i + width; ++j)
            {
                const double a = x[static_cast<std::size_t>(j)];
                const double b = x[static_cast<std::size_t>(j + width)];
                x[static_cast<std::size_t>(j)] = a + b;
                x[static_cast<std::size_t>(j + width)] = a - b;
            }
    for (auto& value : x)
        value *= 0.25;
}

ParameterInfo parameter(const char* id, const char* name, const char* unit, float minimum,
                        float maximum, float defaultValue, ParameterScale scale)
{
    ParameterInfo info;
    info.id = id;
    info.name = name;
    info.unit = unit;
    info.minimum = minimum;
    info.maximum = maximum;
    info.defaultValue = defaultValue;
    info.scale = scale;
    return info;
}

} // namespace

Reverb::Reverb()
{
    description.type = typeId;
    description.name = "Reverb";
    description.parameters = {
        parameter("preDelay", "Pre-delay", "ms", 0.0f, 250.0f, 10.0f, ParameterScale::linear),
        parameter("decay", "Decay", "s", 0.2f, 20.0f, 2.0f, ParameterScale::logarithmic),
        parameter("size", "Size", "%", 0.0f, 100.0f, 70.0f, ParameterScale::linear),
        parameter("damping", "Damping", "%", 0.0f, 100.0f, 40.0f, ParameterScale::linear),
        parameter("lowCut", "Low cut", "Hz", lowCutOff, 1000.0f, 100.0f, ParameterScale::logarithmic),
        parameter("highCut", "High cut", "Hz", 1000.0f, highCutOff, 10000.0f,
                  ParameterScale::logarithmic),
        parameter("width", "Width", "%", 0.0f, 100.0f, 100.0f, ParameterScale::linear),
        parameter("mix", "Mix", "%", 0.0f, 100.0f, 25.0f, ParameterScale::linear),
    };
    description.parameters[size].automatable = false; // it glides instead
}

const EffectDescription& Reverb::getDescription() const noexcept
{
    return description;
}

void Reverb::prepare(const ProcessSetup& setup)
{
    sampleRate = setup.sampleRate;
    const double scale = sampleRate / 48000.0;
    const double depth = modulationDepth * scale;

    memory = 0;
    for (int i = 0; i < numLines; ++i)
    {
        auto& line = lines[static_cast<std::size_t>(i)];
        const auto longest = static_cast<int>(std::ceil(baseLengths[static_cast<std::size_t>(i)] * scale + depth)) + 4;
        line.buffer.assign(static_cast<std::size_t>(longest), 0.0f);
        // Every other line moves, each at its own slow rate (0.1 to 0.8 Hz).
        line.rate = i % 2 == 0 ? 0.0 : 2.0 * std::numbers::pi * (0.1 + 0.1 * (i / 2)) / sampleRate;
        memory = std::max(memory, longest);
    }

    for (std::size_t i = 0; i < diffusers.size(); ++i)
    {
        const auto length = std::max(1, static_cast<int>(std::lround(diffuserLengths[i] * scale)));
        diffusers[i].buffer.assign(static_cast<std::size_t>(length), 0.0f);
        memory += length;
    }

    const auto preDelayLength = static_cast<int>(std::ceil(maximumPreDelaySeconds * sampleRate)) + 1;
    preDelayLine.assign(static_cast<std::size_t>(preDelayLength), 0.0f);
    memory += preDelayLength;

    sizeFor = decayFor = lowCutFor = highCutFor = -1.0f;
    reset();
}

void Reverb::reset() noexcept
{
    for (auto& line : lines)
    {
        std::fill(line.buffer.begin(), line.buffer.end(), 0.0f);
        line.position = 0;
        line.damped = 0.0;
        line.phase = 0.0;
        line.allpass = 0.0;
    }
    for (auto& diffuser : diffusers)
    {
        std::fill(diffuser.buffer.begin(), diffuser.buffer.end(), 0.0f);
        diffuser.position = 0;
    }
    std::fill(preDelayLine.begin(), preDelayLine.end(), 0.0f);
    preDelayPosition = 0;
    lowCutState[0] = lowCutState[1] = highCutState[0] = highCutState[1] = 0.0;
    quietFor = memory * 4;
    started = false;
}

bool Reverb::allSilent(const ProcessBlock& block) noexcept
{
    for (int channel = 0; channel < block.numChannels; ++channel)
        for (int i = 0; i < block.numSamples; ++i)
            if (!juce::exactlyEqual(block.channels[channel][i], 0.0f))
                return false;
    return true;
}

double Reverb::Line::read(double delay) noexcept
{
    // Split into whole samples and a fraction in [0.618, 1.618), where the
    // first-order allpass interpolator behaves best (its coefficient stays small).
    const auto size = static_cast<int>(buffer.size());
    const double total = std::max(1.618, delay);
    const auto whole = static_cast<int>(std::floor(total - 0.618));
    const double fraction = total - whole;
    const double eta = (1.0 - fraction) / (1.0 + fraction);

    const auto at = [this, size](int age) {
        int index = position - age;
        while (index < 0)
            index += size;
        return static_cast<double>(buffer[static_cast<std::size_t>(index)]);
    };

    allpass = at(whole + 1) + eta * (at(whole) - allpass);
    return allpass;
}

double Reverb::Allpass::process(double x) noexcept
{
    const double delayed = buffer[static_cast<std::size_t>(position)];
    const double fed = x + diffusion * delayed;
    buffer[static_cast<std::size_t>(position)] = static_cast<float>(fed);
    position = (position + 1) % static_cast<int>(buffer.size());
    return delayed - diffusion * fed;
}

void Reverb::setSize(float percent, bool glide) noexcept
{
    const double scale = sampleRate / 48000.0 * sizeFactor(percent);
    for (std::size_t i = 0; i < lines.size(); ++i)
    {
        lines[i].target = baseLengths[i] * scale;
        if (!glide)
            lines[i].length = lines[i].target;
    }
    sizeFor = percent;
}

void Reverb::setGains(float decaySeconds) noexcept
{
    // Each pass through a line of d samples loses 60 dB * d / (T60 * rate).
    for (auto& line : lines)
        line.gain = std::pow(10.0, -3.0 * line.length / (std::max(0.01, static_cast<double>(decaySeconds)) * sampleRate));
    decayFor = decaySeconds;
}

void Reverb::process(const ProcessBlock& block) noexcept
{
    const auto& values = block.parameters;
    const int numChannels = std::min(block.numChannels, 2);
    const double depth = modulationDepth * sampleRate / 48000.0;
    const auto preDelaySize = static_cast<int>(preDelayLine.size());

    // At rest with silence coming in: nothing at all to do (the same as going through
    // the samples, which would skip each one).
    if (quietFor >= memory * 4 && allSilent(block))
        return;

    for (int i = 0; i < block.numSamples; ++i)
    {
        const float dryLeft = block.channels[0][i];
        const float dryRight = numChannels > 1 ? block.channels[1][i] : dryLeft;

        // Silence in, nothing left anywhere inside: silence out.
        if (quietFor >= memory * 4 && juce::exactlyEqual(dryLeft, 0.0f) &&
            juce::exactlyEqual(dryRight, 0.0f))
            continue;

        if (!started || !juce::exactlyEqual(values.at(size, i), sizeFor))
        {
            setSize(values.at(size, i), started);
            started = true;
        }

        bool gliding = false;
        for (auto& line : lines)
        {
            if (!juce::exactlyEqual(line.length, line.target))
            {
                const double step = std::clamp(line.target - line.length, -glidePerSample, glidePerSample);
                line.length += step;
                gliding = true;
            }
        }
        if (gliding || !juce::exactlyEqual(values.at(decay, i), decayFor))
            setGains(values.at(decay, i));

        const double dampingAmount = 0.7 * values.at(damping, i) / 100.0;
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

        // Pre-delay, then diffusion.
        const double in = 0.5 * (static_cast<double>(dryLeft) + dryRight);
        preDelayLine[static_cast<std::size_t>(preDelayPosition)] = static_cast<float>(in);
        const auto delaySamples = std::clamp(
            static_cast<int>(std::lround(values.at(preDelay, i) * 0.001 * sampleRate)), 0,
            preDelaySize - 1);
        int readAt = preDelayPosition - delaySamples;
        if (readAt < 0)
            readAt += preDelaySize;
        double diffused = preDelayLine[static_cast<std::size_t>(readAt)];
        preDelayPosition = (preDelayPosition + 1) % preDelaySize;
        for (auto& diffuser : diffusers)
            diffused = diffuser.process(diffused);

        // The network: read, damp, mix, feed back.
        std::array<double, numLines> outs{};
        for (std::size_t k = 0; k < lines.size(); ++k)
        {
            auto& line = lines[k];
            double delay = line.length;
            if (line.rate > 0.0)
            {
                delay += depth * std::sin(line.phase);
                line.phase += line.rate;
                if (line.phase > 2.0 * std::numbers::pi)
                    line.phase -= 2.0 * std::numbers::pi;
            }
            const double read = line.read(std::max(1.0, delay));
            // One-pole low-pass with unity gain at DC: the decay time holds there.
            line.damped = read + dampingAmount * (line.damped - read);
            if (std::abs(line.damped) < restingLevel)
                line.damped = 0.0;
            outs[k] = line.damped * line.gain;
        }

        double left = 0.0;
        double right = 0.0;
        for (std::size_t k = 0; k < lines.size(); ++k)
        {
            left += (k % 2 == 0 ? 1.0 : -1.0) * outs[k];
            right += (k % 4 < 2 ? 1.0 : -1.0) * outs[k];
        }

        auto feedback = outs;
        hadamard(feedback);
        bool anything = !juce::exactlyEqual(diffused, 0.0);
        for (std::size_t k = 0; k < lines.size(); ++k)
        {
            auto& line = lines[k];
            // The input goes in with signs that differ from line to line, so the lines
            // start out different.
            const double sign = (k * 5 + 3) % 7 < 3 ? -1.0 : 1.0;
            const double value = feedback[k] + sign * diffused;
            const auto written = static_cast<float>(std::abs(value) < restingLevel ? 0.0 : value);
            line.buffer[static_cast<std::size_t>(line.position)] = written;
            line.position = (line.position + 1) % static_cast<int>(line.buffer.size());
            anything = anything || !juce::exactlyEqual(written, 0.0f);
        }
        quietFor = anything ? 0 : std::min(quietFor + 1, memory * 4);

        // Width, cuts, mix.
        left *= 0.25;
        right *= 0.25;
        const double spread = values.at(width, i) / 100.0;
        const double mid = 0.5 * (left + right);
        const double side = 0.5 * (left - right) * spread;
        double wet[2] = {mid + side, mid - side};
        for (int channel = 0; channel < 2; ++channel)
        {
            if (highCutHz < highCutOff)
                wet[channel] = lowPass(wet[channel], highCutCoefficient, highCutState[channel]);
            if (lowCutHz > lowCutOff)
                wet[channel] -= lowPass(wet[channel], lowCutCoefficient, lowCutState[channel]);
        }

        const double amount = values.at(mix, i) / 100.0;
        const float dry[2] = {dryLeft, dryRight};
        for (int channel = 0; channel < numChannels; ++channel)
            block.channels[channel][i] =
                static_cast<float>(dry[channel] * (1.0 - amount) + wet[channel] * amount);
    }
}

double Reverb::getTailSeconds(std::span<const float> values) const
{
    const auto at = [&values](int index, float fallback) {
        return static_cast<std::size_t>(index) < values.size() ? values[static_cast<std::size_t>(index)]
                                                               : fallback;
    };
    // 60 dB in the decay time; to 100 dB, 5/3 of it, after the pre-delay.
    return at(preDelay, 10.0f) / 1000.0 + at(decay, 2.0f) * 100.0 / 60.0;
}

} // namespace daw::engine::fx
