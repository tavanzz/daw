#include "daw/engine/fx/Equaliser.h"

#include <algorithm>
#include <cmath>
#include <complex>
#include <numbers>

namespace daw::engine::fx
{

namespace
{

constexpr int parametersPerBand = 5; // on, frequency, gain, q, slope (some unused)
constexpr int outputGainIndex = Equaliser::numBands * parametersPerBand;

// What each band is called, and where it starts.
struct BandSetup
{
    const char* id;
    const char* name;
    Equaliser::BandType type;
    float frequency;
    bool on;
};

constexpr std::array<BandSetup, Equaliser::numBands> bandSetups{{
    {"highPass", "High-pass", Equaliser::BandType::highPass, 30.0f, false},
    {"lowShelf", "Low shelf", Equaliser::BandType::lowShelf, 100.0f, true},
    {"bell1", "Bell 1", Equaliser::BandType::bell, 200.0f, true},
    {"bell2", "Bell 2", Equaliser::BandType::bell, 800.0f, true},
    {"bell3", "Bell 3", Equaliser::BandType::bell, 2500.0f, true},
    {"bell4", "Bell 4", Equaliser::BandType::bell, 6000.0f, true},
    {"highShelf", "High shelf", Equaliser::BandType::highShelf, 8000.0f, true},
    {"lowPass", "Low-pass", Equaliser::BandType::lowPass, 18000.0f, false},
}};

bool hasGain(Equaliser::BandType type) noexcept
{
    return type == Equaliser::BandType::lowShelf || type == Equaliser::BandType::bell ||
           type == Equaliser::BandType::highShelf;
}

int orderOf(int slopeChoice) noexcept
{
    const auto index = static_cast<std::size_t>(std::clamp(slopeChoice, 0, 5));
    return Equaliser::slopes[index] / 6;
}

// Frequencies stay below Nyquist, where tan() would blow up.
double safeFrequency(double frequency, double sampleRate) noexcept
{
    return std::clamp(frequency, 1.0, 0.49 * sampleRate);
}

std::complex<double> analogResponse(Equaliser::BandType type, double omega, double gainDb,
                                    double q, int slopeChoice)
{
    const std::complex<double> s(0.0, omega);
    const double a = std::pow(10.0, gainDb / 40.0);

    switch (type)
    {
    case Equaliser::BandType::bell:
        return (s * s + s * (a / q) + 1.0) / (s * s + s / (a * q) + 1.0);
    case Equaliser::BandType::lowShelf:
    {
        const double sa = std::sqrt(a);
        return a * (s * s + s * (sa / q) + a) / (a * s * s + s * (sa / q) + 1.0);
    }
    case Equaliser::BandType::highShelf:
    {
        const double sa = std::sqrt(a);
        return a * (a * s * s + s * (sa / q) + 1.0) / (s * s + s * (sa / q) + a);
    }
    case Equaliser::BandType::highPass:
    case Equaliser::BandType::lowPass:
    {
        // Butterworth of order n: |H| = 1 / sqrt(1 + w^(+-2n)); the phase does not
        // matter for the magnitude.
        const int order = orderOf(slopeChoice);
        const double w = type == Equaliser::BandType::lowPass ? omega : 1.0 / omega;
        return 1.0 / std::sqrt(1.0 + std::pow(w, 2.0 * order));
    }
    }

    return 1.0;
}

} // namespace

Equaliser::BandType Equaliser::typeOf(int band) noexcept
{
    return bandSetups[static_cast<std::size_t>(std::clamp(band, 0, numBands - 1))].type;
}

Equaliser::BandParameters Equaliser::parametersOf(int band) noexcept
{
    const int base = band * parametersPerBand;
    const auto type = typeOf(band);
    BandParameters parameters;
    parameters.on = base;
    parameters.frequency = base + 1;
    if (hasGain(type))
    {
        parameters.gain = base + 2;
        parameters.q = base + 3;
    }
    else
    {
        parameters.slope = base + 4;
    }
    return parameters;
}

int Equaliser::outputGainParameter() noexcept
{
    return outputGainIndex;
}

Equaliser::Equaliser()
{
    description.type = typeId;
    description.name = "Equaliser";

    for (const auto& setup : bandSetups)
    {
        const juce::String id(setup.id);
        const juce::String name(setup.name);

        description.parameters.push_back(switchParameter(id + "On", name, setup.on));

        ParameterInfo frequency;
        frequency.id = id + "Frequency";
        frequency.name = name + " frequency";
        frequency.unit = "Hz";
        frequency.minimum = 10.0f;
        frequency.maximum = 22000.0f;
        frequency.defaultValue = setup.frequency;
        frequency.scale = ParameterScale::logarithmic;
        description.parameters.push_back(frequency);

        // Every band has the same five parameters, so they are found by position;
        // those a band does not use are not automatable and never shown.
        ParameterInfo gain;
        gain.id = id + "Gain";
        gain.name = name + " gain";
        gain.unit = "dB";
        gain.minimum = -24.0f;
        gain.maximum = 24.0f;
        gain.defaultValue = 0.0f;
        gain.automatable = hasGain(setup.type);
        description.parameters.push_back(gain);

        ParameterInfo q;
        q.id = id + "Q";
        q.name = name + " Q";
        q.minimum = 0.1f;
        q.maximum = 18.0f;
        q.defaultValue = setup.type == BandType::bell ? 1.0f : 0.7071f;
        q.scale = ParameterScale::logarithmic;
        q.automatable = hasGain(setup.type);
        description.parameters.push_back(q);

        auto slope = choiceParameter(id + "Slope", name + " slope",
                                     {"6 dB/oct", "12 dB/oct", "18 dB/oct", "24 dB/oct",
                                      "36 dB/oct", "48 dB/oct"},
                                     1);
        slope.automatable = false;
        description.parameters.push_back(slope);
    }

    ParameterInfo output;
    output.id = "outputGain";
    output.name = "Output";
    output.unit = "dB";
    output.minimum = -24.0f;
    output.maximum = 24.0f;
    output.defaultValue = 0.0f;
    description.parameters.push_back(output);
}

const EffectDescription& Equaliser::getDescription() const noexcept
{
    return description;
}

void Equaliser::prepare(const ProcessSetup& setup)
{
    sampleRate = setup.sampleRate;
    crossfadeLength = std::max(1, static_cast<int>(std::lround(0.01 * sampleRate)));
    reset();
}

void Equaliser::reset() noexcept
{
    for (auto& band : bands)
    {
        band.filter.clear();
        band.previous.clear();
        band.crossfadeLeft = 0;
        band.designedFrequency = -1.0f; // designed again at the next sample
    }
    silent = true;
    fresh = true;
}

namespace
{
// States this small are set to zero, sample by sample (so the result never depends
// on the blocks): far below hearing, and it lets a filter come to rest. Without
// it, flushing denormals can leave a state flipping sign just above the smallest
// normal double for ever.
constexpr double restingLevel = 1.0e-20;

double settled(double state) noexcept
{
    return std::abs(state) < restingLevel ? 0.0 : state;
}
} // namespace

double Equaliser::Section::process(int channel, double x) noexcept
{
    if (firstOrder)
    {
        // Trapezoidal one-pole: a1 is G = g / (1 + g).
        const double v = (x - s1[channel]) * a1;
        const double low = v + s1[channel];
        s1[channel] = settled(low + v);
        return m0 * x + m2 * low;
    }

    const double v3 = x - s2[channel];
    const double v1 = a1 * s1[channel] + a2 * v3;
    const double v2 = s2[channel] + a2 * s1[channel] + a3 * v3;
    s1[channel] = settled(2.0 * v1 - s1[channel]);
    s2[channel] = settled(2.0 * v2 - s2[channel]);
    return m0 * x + m1 * v1 + m2 * v2;
}

void Equaliser::Section::clear() noexcept
{
    s1[0] = s1[1] = s2[0] = s2[1] = 0.0;
}

bool Equaliser::Section::isClear() const noexcept
{
    return juce::exactlyEqual(s1[0], 0.0) && juce::exactlyEqual(s1[1], 0.0) &&
           juce::exactlyEqual(s2[0], 0.0) && juce::exactlyEqual(s2[1], 0.0);
}

void Equaliser::BandFilter::design(BandType type, double frequency, double gainDb, double q,
                                   int slopeChoice, double rate) noexcept
{
    const double g = std::tan(std::numbers::pi * safeFrequency(frequency, rate) / rate);
    const auto setSection = [](Section& section, double gScaled, double k, double m0,
                               double m1, double m2) {
        section.firstOrder = false;
        section.a1 = 1.0 / (1.0 + gScaled * (gScaled + k));
        section.a2 = gScaled * section.a1;
        section.a3 = gScaled * section.a2;
        section.m0 = m0;
        section.m1 = m1;
        section.m2 = m2;
    };

    const double a = std::pow(10.0, gainDb / 40.0);
    q = std::max(q, 0.01);

    switch (type)
    {
    case BandType::bell:
    {
        numSections = 1;
        const double k = 1.0 / (q * a);
        setSection(sections[0], g, k, 1.0, k * (a * a - 1.0), 0.0);
        return;
    }
    case BandType::lowShelf:
    {
        numSections = 1;
        const double k = 1.0 / q;
        setSection(sections[0], g / std::sqrt(a), k, 1.0, k * (a - 1.0), a * a - 1.0);
        return;
    }
    case BandType::highShelf:
    {
        numSections = 1;
        const double k = 1.0 / q;
        setSection(sections[0], g * std::sqrt(a), k, a * a, k * (1.0 - a) * a, 1.0 - a * a);
        return;
    }
    case BandType::highPass:
    case BandType::lowPass:
    {
        // Butterworth: second-order sections with Q = 1 / (2 cos(theta)), plus a
        // one-pole for odd orders.
        const int order = orderOf(slopeChoice);
        const bool high = type == BandType::highPass;
        numSections = 0;

        if (order % 2 == 1)
        {
            auto& section = sections[static_cast<std::size_t>(numSections++)];
            section.firstOrder = true;
            section.a1 = g / (1.0 + g);
            section.m0 = high ? 1.0 : 0.0;
            section.m2 = high ? -1.0 : 1.0;
        }

        for (int i = 1; i <= order / 2; ++i)
        {
            const double theta = order % 2 == 1
                                     ? i * std::numbers::pi / order
                                     : (2 * i - 1) * std::numbers::pi / (2.0 * order);
            const double k = 2.0 * std::cos(theta); // 1 / Q
            if (high)
                setSection(sections[static_cast<std::size_t>(numSections++)], g, k, 1.0, -k, -1.0);
            else
                setSection(sections[static_cast<std::size_t>(numSections++)], g, k, 0.0, 0.0, 1.0);
        }
        return;
    }
    }
}

double Equaliser::BandFilter::process(int channel, double x) noexcept
{
    for (int i = 0; i < numSections; ++i)
        x = sections[static_cast<std::size_t>(i)].process(channel, x);
    return x;
}

void Equaliser::BandFilter::clear() noexcept
{
    for (auto& section : sections)
        section.clear();
}

bool Equaliser::BandFilter::isClear() const noexcept
{
    return std::all_of(sections.begin(), sections.begin() + numSections,
                       [](const Section& section) { return section.isClear(); });
}

void Equaliser::designBand(int index, const ProcessBlock& block, int sample) noexcept
{
    auto& band = bands[static_cast<std::size_t>(index)];
    const auto parameters = parametersOf(index);
    const auto& values = block.parameters;
    const auto type = typeOf(index);

    const float frequency = values.at(parameters.frequency, sample);
    const float gain = parameters.gain >= 0 ? values.at(parameters.gain, sample) : 0.0f;
    const float q = parameters.q >= 0 ? values.at(parameters.q, sample) : 0.7071f;

    if (juce::exactlyEqual(frequency, band.designedFrequency) &&
        juce::exactlyEqual(gain, band.designedGain) && juce::exactlyEqual(q, band.designedQ))
        return;

    band.designedFrequency = frequency;
    band.designedGain = gain;
    band.designedQ = q;
    band.filter.design(type, frequency, gain, q, band.slope, sampleRate);
}

void Equaliser::process(const ProcessBlock& block) noexcept
{
    const auto& values = block.parameters;
    const int outputGain = outputGainParameter();

    // Silence in, filters at rest: silence out, without computing anything.
    bool inputSilent = true;
    for (int channel = 0; channel < block.numChannels && inputSilent; ++channel)
        for (int i = 0; i < block.numSamples && inputSilent; ++i)
            inputSilent = juce::exactlyEqual(block.channels[channel][i], 0.0f);
    if (inputSilent && silent)
        return;

    const int numChannels = std::min(block.numChannels, 2);

    for (int i = 0; i < block.numSamples; ++i)
    {
        double out[2] = {0.0, 0.0};
        for (int channel = 0; channel < numChannels; ++channel)
            out[channel] = block.channels[channel][i];

        for (int index = 0; index < numBands; ++index)
        {
            auto& band = bands[static_cast<std::size_t>(index)];
            const auto parameters = parametersOf(index);
            const bool on = values.at(parameters.on, i) >= 0.5f;
            const int slope = parameters.slope >= 0
                                  ? static_cast<int>(values.at(parameters.slope, i))
                                  : 0;

            // A switch or a new slope: the old setting goes on for the crossfade (but
            // a fresh equaliser starts as it is set).
            if (fresh)
            {
                band.on = on;
                band.slope = slope;
            }
            else if (on != band.on || slope != band.slope)
            {
                band.previous = band.filter;
                band.previousOn = band.on;
                band.crossfadeLeft = crossfadeLength;
                band.on = on;
                band.slope = slope;
                if (on)
                    band.filter.clear();
                band.designedFrequency = -1.0f;
            }

            if (band.on || band.crossfadeLeft > 0)
                designBand(index, block, i);

            for (int channel = 0; channel < numChannels; ++channel)
            {
                const double x = out[channel];
                const double y = band.on ? band.filter.process(channel, x) : x;
                if (band.crossfadeLeft > 0)
                {
                    const double old = band.previousOn ? band.previous.process(channel, x) : x;
                    const double fade = static_cast<double>(band.crossfadeLeft) /
                                        static_cast<double>(crossfadeLength + 1);
                    out[channel] = y + (old - y) * fade;
                }
                else
                {
                    out[channel] = y;
                }
            }

            if (band.crossfadeLeft > 0)
                --band.crossfadeLeft;
        }

        fresh = false;
        const float gainDb = values.at(outputGain, i);
        const double gain = juce::exactlyEqual(gainDb, 0.0f) ? 1.0 : std::pow(10.0, gainDb / 20.0);
        for (int channel = 0; channel < numChannels; ++channel)
            block.channels[channel][i] = static_cast<float>(out[channel] * gain);
    }

    silent = inputSilent && std::all_of(bands.begin(), bands.end(), [](const Band& band) {
                 return band.crossfadeLeft == 0 && (!band.on || band.filter.isClear());
             });
}

double Equaliser::bandMagnitude(std::span<const float> values, int band, double frequency,
                                double rate)
{
    const auto parameters = parametersOf(band);
    const auto type = typeOf(band);
    const auto at = [&values](int index, float fallback) {
        return index >= 0 && static_cast<std::size_t>(index) < values.size()
                   ? values[static_cast<std::size_t>(index)]
                   : fallback;
    };

    const double bandFrequency = safeFrequency(at(parameters.frequency, 1000.0f), rate);
    const double gain = at(parameters.gain, 0.0f);
    const double q = std::max(0.01, static_cast<double>(at(parameters.q, 0.7071f)));
    const int slope = static_cast<int>(at(parameters.slope, 1.0f));

    // The bilinear transform maps frequency f to tan(pi f / rate); prewarping makes
    // the band's frequency land where it should.
    const double omega = std::tan(std::numbers::pi * std::clamp(frequency, 0.0, 0.4999 * rate) / rate) /
                         std::tan(std::numbers::pi * bandFrequency / rate);
    return std::abs(analogResponse(type, omega, gain, q, slope));
}

double Equaliser::magnitude(std::span<const float> values, double frequency, double rate)
{
    double total = 1.0;
    for (int band = 0; band < numBands; ++band)
    {
        const auto on = static_cast<std::size_t>(parametersOf(band).on);
        if (on < values.size() && values[on] >= 0.5f)
            total *= bandMagnitude(values, band, frequency, rate);
    }

    const auto output = static_cast<std::size_t>(outputGainParameter());
    if (output < values.size())
        total *= std::pow(10.0, static_cast<double>(values[output]) / 20.0);
    return total;
}

} // namespace daw::engine::fx
