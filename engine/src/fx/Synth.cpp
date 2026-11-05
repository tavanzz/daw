#include "daw/engine/fx/Synth.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <numbers>

namespace daw::engine::fx
{

namespace
{

ParameterInfo linear(const char* id, const char* name, const char* unit, float minimum,
                     float maximum, float defaultValue)
{
    ParameterInfo info;
    info.id = id;
    info.name = name;
    info.unit = unit;
    info.minimum = minimum;
    info.maximum = maximum;
    info.defaultValue = defaultValue;
    return info;
}

ParameterInfo logarithmic(const char* id, const char* name, const char* unit, float minimum,
                          float maximum, float defaultValue)
{
    auto info = linear(id, name, unit, minimum, maximum, defaultValue);
    info.scale = ParameterScale::logarithmic;
    return info;
}

juce::StringArray numbers(int from, int to, bool signs)
{
    juce::StringArray names;
    for (int value = from; value <= to; ++value)
        names.add(signs && value > 0 ? "+" + juce::String(value) : juce::String(value));
    return names;
}

// Note values for the LFO, in quarter notes per cycle.
struct LfoNote
{
    const char* name;
    double quarters;
};
constexpr std::array<LfoNote, 14> lfoNotes{{{"1/1", 4.0},
                                           {"1/2", 2.0},
                                           {"1/4", 1.0},
                                           {"1/8", 0.5},
                                           {"1/16", 0.25},
                                           {"1/32", 0.125},
                                           {"1/2T", 4.0 / 3.0},
                                           {"1/4T", 2.0 / 3.0},
                                           {"1/8T", 1.0 / 3.0},
                                           {"1/16T", 1.0 / 6.0},
                                           {"1/2D", 3.0},
                                           {"1/4D", 1.5},
                                           {"1/8D", 0.75},
                                           {"1/16D", 0.375}}};

std::uint32_t nextRandom(std::uint32_t& state) noexcept
{
    // xorshift32: deterministic, so exports equal playback.
    state ^= state << 13;
    state ^= state >> 17;
    state ^= state << 5;
    return state;
}

float randomBipolar(std::uint32_t& state) noexcept
{
    return static_cast<float>(nextRandom(state)) / 2147483648.0f - 1.0f;
}

double frequencyOf(double semitones) noexcept
{
    return 440.0 * std::exp2((semitones - 69.0) / 12.0);
}

// The resonance is held in check as in an analog filter. The band-pass state of the
// resonant stage is exactly linear up to the knee, then bends smoothly (slope 1 at
// the knee, falling as 1/(1+u)^2) towards the limit it never reaches: a resonant
// peak of up to 26 dB (Q 20) cannot grow without bound, while everything below the
// knee is untouched. Levels are those of the oscillators (a full saw is 1).
constexpr double resonanceKnee = 0.7;
constexpr double resonanceLimit = 1.4;

double saturatedBand(double band) noexcept
{
    const double magnitude = std::abs(band);
    if (magnitude <= resonanceKnee)
        return band;
    const double u = (magnitude - resonanceKnee) / (resonanceLimit - resonanceKnee);
    return std::copysign(resonanceKnee + (resonanceLimit - resonanceKnee) * u / (1.0 + u), band);
}

// The filter's input is turned down as the resonance makes the low-pass response
// peak: by a fraction of that peak in dB (none up to Q = 1/sqrt 2, where it starts to
// peak; 6.5 dB at Q 20), so the passband and the resonance stay at a level together.
constexpr double compensatedFraction = 0.25;

double inputGainFor(double q) noexcept
{
    if (q <= 1.0 / std::numbers::sqrt2)
        return 1.0;
    const double peak = q / std::sqrt(1.0 - 1.0 / (4.0 * q * q)); // the 12 dB low-pass's
    return std::pow(peak, -compensatedFraction);
}

} // namespace

Synth::Synth()
{
    description.type = typeId;
    description.name = "Synth";
    description.isInstrument = true;

    const juce::StringArray waveNames{"Sine", "Triangle", "Saw", "Pulse"};
    auto& p = description.parameters;
    p.resize(numParameters);
    const auto set = [&p](Parameter index, ParameterInfo info) {
        p[static_cast<std::size_t>(index)] = std::move(info);
    };

    set(osc1Wave, choiceParameter("osc1Wave", "Osc 1 Wave", waveNames, 2));
    set(osc1Octave, choiceParameter("osc1Octave", "Osc 1 Octave", numbers(-3, 3, true), 3));
    set(osc1Semitone, choiceParameter("osc1Semitone", "Osc 1 Semitone", numbers(-12, 12, true), 12));
    set(osc1Fine, linear("osc1Fine", "Osc 1 Fine", "ct", -100.0f, 100.0f, 0.0f));
    set(osc1Width, linear("osc1Width", "Osc 1 Width", "%", 5.0f, 95.0f, 50.0f));
    set(osc1Level, linear("osc1Level", "Osc 1 Level", "%", 0.0f, 100.0f, 70.0f));
    set(osc2Wave, choiceParameter("osc2Wave", "Osc 2 Wave", waveNames, 2));
    set(osc2Octave, choiceParameter("osc2Octave", "Osc 2 Octave", numbers(-3, 3, true), 3));
    set(osc2Semitone, choiceParameter("osc2Semitone", "Osc 2 Semitone", numbers(-12, 12, true), 12));
    set(osc2Fine, linear("osc2Fine", "Osc 2 Fine", "ct", -100.0f, 100.0f, 7.0f));
    set(osc2Width, linear("osc2Width", "Osc 2 Width", "%", 5.0f, 95.0f, 50.0f));
    set(osc2Level, linear("osc2Level", "Osc 2 Level", "%", 0.0f, 100.0f, 70.0f));
    set(noiseLevel, linear("noiseLevel", "Noise Level", "%", 0.0f, 100.0f, 0.0f));

    set(filterType, choiceParameter("filterType", "Filter Type",
                                    {"Low-pass 12", "Low-pass 24", "Band-pass", "High-pass"}, 1));
    set(cutoff, logarithmic("cutoff", "Cutoff", "Hz", 20.0f, 20000.0f, 2000.0f));
    set(resonance, linear("resonance", "Resonance", "%", 0.0f, 100.0f, 20.0f));
    set(keyTracking, linear("keyTracking", "Key Tracking", "%", 0.0f, 100.0f, 50.0f));
    set(filterEnvelope, linear("filterEnvelope", "Envelope Amount", "st", -96.0f, 96.0f, 24.0f));
    set(filterVelocity, linear("filterVelocity", "Velocity to Cutoff", "st", 0.0f, 48.0f, 12.0f));

    set(ampAttack, logarithmic("ampAttack", "Amp Attack", "ms", 0.1f, 10000.0f, 5.0f));
    set(ampDecay, logarithmic("ampDecay", "Amp Decay", "ms", 1.0f, 20000.0f, 300.0f));
    set(ampSustain, linear("ampSustain", "Amp Sustain", "%", 0.0f, 100.0f, 80.0f));
    set(ampRelease, logarithmic("ampRelease", "Amp Release", "ms", 1.0f, 20000.0f, 300.0f));
    set(ampVelocity, linear("ampVelocity", "Velocity to Level", "%", 0.0f, 100.0f, 70.0f));
    set(modAttack, logarithmic("modAttack", "Mod Attack", "ms", 0.1f, 10000.0f, 5.0f));
    set(modDecay, logarithmic("modDecay", "Mod Decay", "ms", 1.0f, 20000.0f, 400.0f));
    set(modSustain, linear("modSustain", "Mod Sustain", "%", 0.0f, 100.0f, 30.0f));
    set(modRelease, logarithmic("modRelease", "Mod Release", "ms", 1.0f, 20000.0f, 400.0f));
    set(modToPitch, linear("modToPitch", "Mod to Pitch", "st", -24.0f, 24.0f, 0.0f));

    set(lfoWave, choiceParameter("lfoWave", "LFO Wave",
                                 {"Sine", "Triangle", "Saw", "Square", "Random"}, 0));
    set(lfoSync, switchParameter("lfoSync", "LFO Sync", false));
    set(lfoRate, logarithmic("lfoRate", "LFO Rate", "Hz", 0.05f, 20.0f, 5.0f));
    juce::StringArray noteNames;
    for (const auto& note : lfoNotes)
        noteNames.add(note.name);
    set(lfoNote, choiceParameter("lfoNote", "LFO Note", noteNames, 2));
    set(lfoToPitch, linear("lfoToPitch", "LFO to Pitch", "ct", 0.0f, 1200.0f, 0.0f));
    set(lfoToCutoff, linear("lfoToCutoff", "LFO to Cutoff", "st", 0.0f, 48.0f, 0.0f));
    set(lfoToLevel, linear("lfoToLevel", "LFO to Level", "%", 0.0f, 100.0f, 0.0f));
    set(lfoToWidth, linear("lfoToWidth", "LFO to Width", "%", 0.0f, 45.0f, 0.0f));
    set(wheelToVibrato, linear("wheelToVibrato", "Mod Wheel to Vibrato", "ct", 0.0f, 100.0f, 50.0f));

    set(voiceMode, choiceParameter("voiceMode", "Voices", {"Poly", "Mono", "Legato"}, 0));
    set(polyphony, choiceParameter("polyphony", "Polyphony", numbers(1, maximumVoices, false), 15));
    set(glide, linear("glide", "Glide", "ms", 0.0f, 2000.0f, 0.0f));
    set(bendRange, choiceParameter("bendRange", "Bend Range", numbers(0, 24, false), 2));
    set(level, linear("level", "Level", "dB", -24.0f, 6.0f, -6.0f));

    // Sections for the editor.
    for (std::size_t i = 0; i < p.size(); ++i)
    {
        const auto& id = p[i].id;
        p[i].group = id.startsWith("osc1")   ? "Oscillator 1"
                     : id.startsWith("osc2") ? "Oscillator 2"
                     : id == "noiseLevel"    ? "Oscillator 2"
                     : id.startsWith("amp")  ? "Envelope 1 (Level)"
                     : id.startsWith("mod")  ? "Envelope 2 (Filter, Pitch)"
                     : id.startsWith("lfo") || id == "wheelToVibrato" ? "LFO"
                     : (id == "voiceMode" || id == "polyphony" || id == "glide" ||
                        id == "bendRange" || id == "level")
                         ? "Voices"
                         : "Filter";
    }

    for (auto& value : controls.values)
        value = 0.0f;
}

const EffectDescription& Synth::getDescription() const noexcept
{
    return description;
}

void Synth::prepare(const ProcessSetup& setup)
{
    sampleRate = setup.sampleRate;
    waves = &dsp::BandLimitedWaves::get();
    for (std::size_t i = 0; i < description.parameters.size(); ++i)
        controls.values[i] = description.parameters[i].defaultValue;
    reset();
}

void Synth::reset() noexcept
{
    for (auto& voice : voices)
    {
        voice.active = false;
        voice.amp.reset();
        voice.mod.reset();
    }
    clock = 0;
    lastTick = std::numeric_limits<std::uint64_t>::max();
    notesStarted = 0;
    lfoPhase = 0.0;
    lfoValue = 0.0f;
    lfoRandom = 0.0f;
    lfoNoise = 0x12345678u;
    bend = 0.0f;
    wheel = 0.0f;
    pressure = 0.0f;
    sustainDown = false;
    volume = 1.0;
    volumeTarget = 1.0;
    cc7 = 1.0f;
    cc11 = 1.0f;
    lastPitch = -1.0;
    numHeld = 0;
}

double Synth::getTailSeconds(std::span<const float> values) const
{
    const auto release = values.size() > static_cast<std::size_t>(ampRelease)
                             ? static_cast<double>(values[static_cast<std::size_t>(ampRelease)])
                             : 300.0;
    // The release loses 60 dB in its time; the voice ends at -100 dB.
    return release / 1000.0 * 100.0 / 60.0 + 0.01;
}

int Synth::getActiveVoices() const noexcept
{
    return static_cast<int>(std::count_if(voices.begin(), voices.end(),
                                          [](const Voice& voice) { return voice.active; }));
}

int Synth::polyphonyNow() const noexcept
{
    return std::clamp(static_cast<int>(controls[polyphony]) + 1, 1, maximumVoices);
}

Synth::VoiceMode Synth::modeNow() const noexcept
{
    return static_cast<VoiceMode>(std::clamp(static_cast<int>(controls[voiceMode]), 0, 2));
}

void Synth::readControls(const ProcessBlock& block, int sample) noexcept
{
    const int count = std::min(block.parameters.size(), static_cast<int>(numParameters));
    for (int p = 0; p < count; ++p)
        controls.values[static_cast<std::size_t>(p)] = block.parameters.at(p, sample);
}

void Synth::process(const ProcessBlock& block) noexcept
{
    // At each sample: the control tick (if one falls there) first, so that events
    // there see the parameters of their own sample; then the events; then the sound
    // up to the next event or tick.
    std::size_t next = 0;
    int sample = 0;
    while (sample < block.numSamples)
    {
        if (clock % controlInterval == 0 && clock != lastTick)
        {
            readControls(block, sample);
            controlTick(block.time);
            for (auto& voice : voices)
                if (voice.active)
                    voiceControl(voice, controlInterval);
            lastTick = clock;
        }

        for (; next < block.events.size() && block.events[next].offset <= sample; ++next)
            handle(block.events[next]);

        const int until = next < block.events.size()
                              ? std::min(block.numSamples, block.events[next].offset)
                              : block.numSamples;
        const auto intoInterval = static_cast<int>(clock % controlInterval);
        const int length = std::min(until - sample, controlInterval - intoInterval);
        if (length <= 0)
            continue;
        render(block.channels, block.numChannels, sample, length);
        sample += length;
        clock += static_cast<std::uint64_t>(length);
    }

    for (; next < block.events.size(); ++next)
        handle(block.events[next]);
}

void Synth::controlTick(const TimeInfo& time) noexcept
{
    double rate = controls[lfoRate];
    if (controls[lfoSync] >= 0.5f)
    {
        const auto& note =
            lfoNotes[static_cast<std::size_t>(std::clamp(static_cast<int>(controls[lfoNote]), 0,
                                                         static_cast<int>(lfoNotes.size()) - 1))];
        rate = time.tempo / 60.0 / note.quarters;
    }

    lfoPhase += rate * controlInterval / sampleRate;
    if (lfoPhase >= 1.0)
    {
        lfoPhase -= std::floor(lfoPhase);
        lfoRandom = randomBipolar(lfoNoise);
    }

    const auto phase = static_cast<float>(lfoPhase);
    switch (std::clamp(static_cast<int>(controls[lfoWave]), 0, 4))
    {
    case 0:
        lfoValue = std::sin(2.0f * std::numbers::pi_v<float> * phase);
        break;
    case 1:
        lfoValue = phase < 0.5f ? 4.0f * phase - 1.0f : 3.0f - 4.0f * phase;
        break;
    case 2:
        lfoValue = 2.0f * phase - 1.0f;
        break;
    case 3:
        lfoValue = phase < 0.5f ? 1.0f : -1.0f;
        break;
    default:
        lfoValue = lfoRandom;
        break;
    }

    // Volume and expression: gain = (cc7 * cc11)^2 (40 log10, as General MIDI), glided
    // so that it never steps.
    volumeTarget = std::pow(static_cast<double>(cc7) * static_cast<double>(cc11), 2.0);
    volume += (volumeTarget - volume) * 0.25;
}

void Synth::voiceControl(Voice& voice, int length) noexcept
{
    const auto& c = controls;
    const double fs = sampleRate;

    // Pitch: the glide, then everything that moves it.
    if (voice.glideDone < voice.glideLength)
    {
        voice.pitch = voice.glideFrom + (voice.key - voice.glideFrom) *
                                            (static_cast<double>(voice.glideDone) / voice.glideLength);
        voice.glideDone += controlInterval;
    }
    else
    {
        voice.pitch = voice.key;
    }

    voice.amp.setTimes(c[ampAttack] / 1000.0, c[ampDecay] / 1000.0, c[ampSustain] / 100.0,
                       c[ampRelease] / 1000.0, fs);
    voice.mod.setTimes(c[modAttack] / 1000.0, c[modDecay] / 1000.0, c[modSustain] / 100.0,
                       c[modRelease] / 1000.0, fs);
    const double modLevel = voice.mod.getLevel();

    const double vibrato = lfoValue * (c[lfoToPitch] + wheel * c[wheelToVibrato]) / 100.0;
    const double common = voice.pitch + bend * std::round(c[bendRange]) + modLevel * c[modToPitch] +
                          vibrato;
    const double semis1 = common + 12.0 * (std::round(c[osc1Octave]) - 3.0) +
                          (std::round(c[osc1Semitone]) - 12.0) + c[osc1Fine] / 100.0;
    const double semis2 = common + 12.0 * (std::round(c[osc2Octave]) - 3.0) +
                          (std::round(c[osc2Semitone]) - 12.0) + c[osc2Fine] / 100.0;
    const double target1 = std::min(frequencyOf(semis1) / fs, 0.49);
    const double target2 = std::min(frequencyOf(semis2) / fs, 0.49);

    // Increments glide to their targets over the interval; band limits for the
    // higher end of it.
    if (!voice.filterStarted)
    {
        voice.increment1 = target1;
        voice.increment2 = target2;
    }
    voice.incrementStep1 = (target1 - voice.increment1) / length;
    voice.incrementStep2 = (target2 - voice.increment2) / length;
    voice.selection1 = waves->select(std::max(voice.increment1, target1));
    voice.selection2 = waves->select(std::max(voice.increment2, target2));

    const float lfoWidth = lfoValue * c[lfoToWidth];
    voice.width1 = std::clamp((c[osc1Width] + lfoWidth) / 100.0f, 0.05f, 0.95f);
    voice.width2 = std::clamp((c[osc2Width] + lfoWidth) / 100.0f, 0.05f, 0.95f);

    // Cutoff: keys, envelope 2, velocity and the LFO, in semitones.
    const double shift = c[keyTracking] / 100.0 * (voice.key - 60) + c[filterEnvelope] * modLevel +
                         c[filterVelocity] * voice.velocity + c[lfoToCutoff] * lfoValue;
    const double cutoffHz =
        std::clamp(static_cast<double>(c[cutoff]) * std::exp2(shift / 12.0), 10.0, 0.45 * fs);
    const double gTarget = std::tan(std::numbers::pi * cutoffHz / fs);
    if (!voice.filterStarted)
        voice.g = gTarget;
    voice.gStep = (gTarget - voice.g) / length;
    const double q = 0.5 * std::pow(40.0, c[resonance] / 100.0); // 0.5 to 20
    voice.k = 1.0 / q;
    voice.inputGain = inputGainFor(q);
    voice.filterStarted = true;
}

void Synth::render(float* const* channels, int numChannels, int start, int length) noexcept
{
    bool any = false;
    float* mix = mixScratch.data();
    for (auto& voice : voices)
    {
        if (!voice.active)
            continue;
        if (!any)
            std::fill(mix, mix + length, 0.0f);
        any = true;
        renderVoice(voice, mix, length);
    }
    if (!any)
        return;

    const auto gain = static_cast<float>(std::pow(10.0, controls[level] / 20.0) * volume);
    for (int channel = 0; channel < numChannels; ++channel)
        for (int i = 0; i < length; ++i)
            channels[channel][start + i] += mix[i] * gain;
}

void Synth::renderVoice(Voice& voice, float* out, int length) noexcept
{
    const auto& c = controls;
    using Waves = dsp::BandLimitedWaves;
    const auto wave1 = static_cast<Wave>(std::clamp(static_cast<int>(c[osc1Wave]), 0, 3));
    const auto wave2 = static_cast<Wave>(std::clamp(static_cast<int>(c[osc2Wave]), 0, 3));
    const auto type = static_cast<FilterType>(std::clamp(static_cast<int>(c[filterType]), 0, 3));
    const float level1 = c[osc1Level] / 100.0f;
    const float level2 = c[osc2Level] / 100.0f;
    const float levelNoise = c[noiseLevel] / 100.0f;
    const float velocityGain = 1.0f - c[ampVelocity] / 100.0f * (1.0f - voice.velocity);
    const float tremolo = 1.0f - c[lfoToLevel] / 100.0f * (0.5f - 0.5f * lfoValue);
    constexpr double secondStageK = 1.4142135623730951; // Butterworth

    const auto oscillator = [this](Wave wave, double phase, float width,
                                   const Waves::Selection& selection) {
        switch (wave)
        {
        case Wave::sine:
            return waves->read(Waves::Wave::sine, phase, selection);
        case Wave::triangle:
            return waves->read(Waves::Wave::triangle, phase, selection);
        case Wave::saw:
            return waves->read(Waves::Wave::saw, phase, selection);
        case Wave::pulse:
            break;
        }
        // The difference of two saws a width apart: a pulse, its mean 0.
        double shifted = phase - width;
        if (shifted < 0.0)
            shifted += 1.0;
        return waves->read(Waves::Wave::saw, phase, selection) -
               waves->read(Waves::Wave::saw, shifted, selection);
    };

    const auto filterStage = [](Filter& state, double input, double g, double k, bool resonant,
                                double& band, double& high) {
        const double a1 = 1.0 / (1.0 + g * (g + k));
        const double a2 = g * a1;
        const double a3 = g * a2;
        const double v3 = input - state.ic2;
        const double linear = a1 * state.ic1 + a2 * v3;
        const double v1 = resonant ? saturatedBand(linear) : linear;
        // v2 = ic2 + g v1; written so that it is the linear filter's to the bit below
        // the knee.
        double v2 = state.ic2 + a2 * state.ic1 + a3 * v3;
        if (v1 != linear)
            v2 += g * (v1 - linear);
        state.ic1 = 2.0 * v1 - state.ic1;
        state.ic2 = 2.0 * v2 - state.ic2;
        band = v1;
        high = input - k * v1 - v2;
        return v2;
    };

    for (int i = 0; i < length; ++i)
    {
        double x = 0.0;
        if (level1 > 0.0f)
            x += level1 * oscillator(wave1, voice.phase1, voice.width1, voice.selection1);
        if (level2 > 0.0f)
            x += level2 * oscillator(wave2, voice.phase2, voice.width2, voice.selection2);
        if (levelNoise > 0.0f)
            x += levelNoise * randomBipolar(voice.noise);

        double band = 0.0;
        double high = 0.0;
        // The band-pass, unity at its centre, never peaks: neither compensated nor
        // saturated.
        const bool resonant = type != FilterType::bandPass;
        if (resonant)
            x *= voice.inputGain;
        double y = filterStage(voice.stage1, x, voice.g, voice.k, resonant, band, high);
        switch (type)
        {
        case FilterType::lowPass12:
            break;
        case FilterType::lowPass24:
        {
            double band2 = 0.0;
            double high2 = 0.0;
            y = filterStage(voice.stage2, y, voice.g, secondStageK, false, band2, high2);
            break;
        }
        case FilterType::bandPass:
            y = band * voice.k; // unity gain at the centre
            break;
        case FilterType::highPass:
            y = high;
            break;
        }

        const double amp = voice.amp.next();
        voice.mod.next();
        out[i] += static_cast<float>(y * amp) * velocityGain * tremolo * voice.fade;

        voice.phase1 += voice.increment1;
        voice.phase1 -= std::floor(voice.phase1);
        voice.phase2 += voice.increment2;
        voice.phase2 -= std::floor(voice.phase2);
        voice.increment1 += voice.incrementStep1;
        voice.increment2 += voice.incrementStep2;
        voice.g += voice.gStep;

        if (voice.fadeStep > 0.0f)
        {
            voice.fade -= voice.fadeStep;
            if (voice.fade <= 0.0f)
            {
                voice.active = false;
                return;
            }
        }
    }

    // Values that die away below what floats hold are set to zero, or flushing
    // denormals could leave the filter ringing for ever.
    for (auto* state : {&voice.stage1, &voice.stage2})
    {
        if (std::abs(state->ic1) < 1.0e-20)
            state->ic1 = 0.0;
        if (std::abs(state->ic2) < 1.0e-20)
            state->ic2 = 0.0;
    }

    if (!voice.amp.isActive())
        voice.active = false;
}

void Synth::handle(const midi::Event& event) noexcept
{
    switch (event.type)
    {
    case midi::EventType::noteOn:
        noteOn(event);
        break;
    case midi::EventType::noteOff:
        noteOff(event);
        break;
    case midi::EventType::pitchBend:
        bend = std::clamp(event.value, -1.0f, 1.0f);
        break;
    case midi::EventType::channelPressure:
        pressure = event.value;
        break;
    case midi::EventType::controller:
        if (event.number == midi::controllers::modWheel)
            wheel = event.value;
        else if (event.number == midi::controllers::volume)
            cc7 = event.value;
        else if (event.number == midi::controllers::expression)
            cc11 = event.value;
        else if (event.number == midi::controllers::sustain)
            setSustain(event.value >= 0.5f);
        break;
    }
}

void Synth::startVoice(Voice& voice, const midi::Event& event, bool retrigger) noexcept
{
    const bool fresh = !voice.active || !retrigger;
    voice.active = true;
    voice.held = true;
    voice.sustained = false;
    voice.key = event.number;
    voice.channel = event.channel;
    voice.noteId = event.noteId;
    voice.velocity = std::clamp(event.value, 0.0f, 1.0f);
    voice.order = ++notesStarted;

    const double glideSeconds = controls[glide] / 1000.0;
    if (glideSeconds > 0.0 && lastPitch >= 0.0)
    {
        voice.glideFrom = fresh ? lastPitch : voice.pitch;
        voice.glideLength = std::max(1, static_cast<int>(std::lround(glideSeconds * sampleRate)));
        voice.glideDone = 0;
    }
    else
    {
        voice.glideLength = 0;
        voice.glideDone = 0;
    }

    if (fresh)
    {
        voice.phase1 = 0.0;
        voice.phase2 = 0.0;
        voice.stage1 = {};
        voice.stage2 = {};
        voice.filterStarted = false;
        voice.noise = static_cast<std::uint32_t>(voice.order * 2654435761u) | 1u;
        voice.amp.reset();
        voice.mod.reset();
        voice.fade = 1.0f;
        voice.fadeStep = 0.0f;
    }

    voice.amp.noteOn();
    voice.mod.noteOn();
    lastPitch = voice.key;
    voiceControl(voice, controlInterval - static_cast<int>(clock % controlInterval));
}

void Synth::releaseVoice(Voice& voice) noexcept
{
    voice.held = false;
    voice.sustained = false;
    voice.amp.noteOff();
    voice.mod.noteOff();
}

int Synth::takeVoice() noexcept
{
    const int count = polyphonyNow();
    for (int i = 0; i < count; ++i)
        if (!voices[static_cast<std::size_t>(i)].active)
            return i;

    // Every voice is taken: the quietest releasing one, or else the oldest.
    int quietest = -1;
    int oldest = 0;
    for (int i = 0; i < count; ++i)
    {
        const auto& voice = voices[static_cast<std::size_t>(i)];
        if (!voice.held && !voice.sustained &&
            (quietest < 0 ||
             voice.amp.getLevel() < voices[static_cast<std::size_t>(quietest)].amp.getLevel()))
            quietest = i;
        if (voice.order < voices[static_cast<std::size_t>(oldest)].order)
            oldest = i;
    }
    const int stolen = quietest >= 0 ? quietest : oldest;
    stealInto(voices[static_cast<std::size_t>(stolen)]);
    return stolen;
}

void Synth::stealInto(Voice& voice) noexcept
{
    // The stolen sound goes on, fading out, on a spare voice (the one closest to its
    // end if every spare is busy).
    int spare = maximumVoices;
    for (int i = maximumVoices; i < maximumVoices + spareVoices; ++i)
    {
        const auto& candidate = voices[static_cast<std::size_t>(i)];
        if (!candidate.active)
        {
            spare = i;
            break;
        }
        if (candidate.fade < voices[static_cast<std::size_t>(spare)].fade)
            spare = i;
    }

    auto& fading = voices[static_cast<std::size_t>(spare)];
    fading = voice;
    fading.held = false;
    fading.sustained = false;
    fading.noteId = -1;
    fading.fadeStep = static_cast<float>(1.0 / std::max(1.0, stealFadeSeconds * sampleRate));
    voice.active = false;
}

void Synth::noteOn(const midi::Event& event) noexcept
{
    const auto mode = modeNow();
    if (mode == VoiceMode::poly)
    {
        // A key struck again takes the voice it sounds on.
        for (int i = 0; i < maximumVoices; ++i)
        {
            auto& voice = voices[static_cast<std::size_t>(i)];
            if (voice.active && voice.fadeStep <= 0.0f && voice.key == event.number &&
                voice.channel == event.channel)
            {
                startVoice(voice, event, true);
                return;
            }
        }
        startVoice(voices[static_cast<std::size_t>(takeVoice())], event, false);
        return;
    }

    // Mono and legato: one voice; the keys held are remembered for when the newest
    // one is released.
    if (numHeld < static_cast<int>(heldKeys.size()))
    {
        heldKeys[static_cast<std::size_t>(numHeld)] = event.number;
        heldIds[static_cast<std::size_t>(numHeld)] = event.noteId;
        ++numHeld;
    }

    auto& voice = voices[0];
    if (voice.active && voice.held && numHeld > 1)
    {
        moveMonoVoice(voice, event.number, event.noteId, mode == VoiceMode::mono);
        return;
    }
    startVoice(voice, event, voice.active);
}

void Synth::moveMonoVoice(Voice& voice, int key, std::int32_t noteId, bool retrigger) noexcept
{
    // The one voice moves to another key: gliding if glide is on; in mono the
    // envelopes start again (from where they are), in legato they go on.
    voice.glideFrom = voice.pitch;
    const double glideSeconds = controls[glide] / 1000.0;
    voice.glideLength =
        glideSeconds > 0.0 ? std::max(1, static_cast<int>(std::lround(glideSeconds * sampleRate))) : 0;
    voice.glideDone = 0;
    voice.key = key;
    voice.noteId = noteId;
    voice.held = true;
    voice.sustained = false;
    lastPitch = key;
    if (retrigger)
    {
        voice.amp.noteOn();
        voice.mod.noteOn();
    }
}

void Synth::noteOff(const midi::Event& event) noexcept
{
    const auto matches = [&event](const Voice& voice) {
        return voice.active && voice.held && voice.fadeStep <= 0.0f &&
               (event.noteId >= 0 && voice.noteId >= 0
                    ? voice.noteId == event.noteId
                    : voice.key == event.number && voice.channel == event.channel);
    };

    if (modeNow() == VoiceMode::poly)
    {
        for (int i = 0; i < maximumVoices; ++i)
        {
            auto& voice = voices[static_cast<std::size_t>(i)];
            if (!matches(voice))
                continue;
            if (sustainDown)
            {
                voice.held = false;
                voice.sustained = true;
            }
            else
            {
                releaseVoice(voice);
            }
            return;
        }
        return;
    }

    // Mono and legato.
    for (int i = 0; i < numHeld; ++i)
    {
        const bool same = event.noteId >= 0 && heldIds[static_cast<std::size_t>(i)] >= 0
                              ? heldIds[static_cast<std::size_t>(i)] == event.noteId
                              : heldKeys[static_cast<std::size_t>(i)] == event.number;
        if (!same)
            continue;
        for (int j = i + 1; j < numHeld; ++j)
        {
            heldKeys[static_cast<std::size_t>(j - 1)] = heldKeys[static_cast<std::size_t>(j)];
            heldIds[static_cast<std::size_t>(j - 1)] = heldIds[static_cast<std::size_t>(j)];
        }
        --numHeld;
        break;
    }

    auto& voice = voices[0];
    if (!voice.active || voice.key != event.number)
        return;

    if (numHeld > 0)
    {
        // Back to the newest key still held.
        const auto back = static_cast<std::size_t>(numHeld - 1);
        moveMonoVoice(voice, heldKeys[back], heldIds[back], modeNow() == VoiceMode::mono);
        return;
    }

    if (sustainDown)
    {
        voice.held = false;
        voice.sustained = true;
    }
    else
    {
        releaseVoice(voice);
    }
}

void Synth::setSustain(bool down) noexcept
{
    sustainDown = down;
    if (down)
        return;
    for (auto& voice : voices)
        if (voice.active && voice.sustained)
            releaseVoice(voice);
}

} // namespace daw::engine::fx
