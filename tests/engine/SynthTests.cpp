#include "daw/engine/Engine.h"
#include "daw/engine/OfflineRenderer.h"
#include "daw/engine/dsp/Envelope.h"
#include "daw/engine/fx/Synth.h"

#include "support/InstrumentRunner.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <bit>
#include <cmath>
#include <functional>
#include <memory>
#include <numbers>
#include <vector>

namespace midi = daw::engine::midi;
using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using daw::engine::dsp::Envelope;
using daw::engine::fx::Synth;
using daw::test::InstrumentRunner;
using daw::test::measuredFrequency;

namespace
{

// One oscillator, nothing else moving: the filter open, full level, a steady note.
std::unique_ptr<InstrumentRunner> plainSynth(double rate, float wave, int maximumBlock = 512)
{
    auto runner = std::make_unique<InstrumentRunner>(std::make_unique<Synth>(), rate, maximumBlock);
    runner->set("osc1Wave", wave);
    runner->set("osc2Level", 0.0f);
    runner->set("osc1Level", 100.0f);
    runner->set("filterType", 0.0f); // low-pass 12
    runner->set("cutoff", 20000.0f);
    runner->set("resonance", 0.0f);
    runner->set("keyTracking", 0.0f);
    runner->set("filterEnvelope", 0.0f);
    runner->set("filterVelocity", 0.0f);
    runner->set("ampAttack", 0.1f);
    runner->set("ampSustain", 100.0f);
    runner->set("ampVelocity", 0.0f);
    runner->set("level", 0.0f);
    return runner;
}

double frequencyOfKey(double key)
{
    return 440.0 * std::exp2((key - 69.0) / 12.0);
}

double cents(double measured, double expected)
{
    return 1200.0 * std::log2(measured / expected);
}

} // namespace

TEST_CASE("Envelopes take exactly the times they are set to", "[engine][synth]")
{
    constexpr double rate = 48000.0;
    Envelope envelope;
    envelope.setTimes(0.1, 0.2, 0.5, 0.3, rate);
    envelope.noteOn();

    // Attack: a straight line to 1 in 4800 samples.
    for (int i = 1; i <= 4800; ++i)
    {
        const double level = envelope.next();
        if (i == 2400)
            CHECK_THAT(level, WithinAbs(0.5, 1e-9));
    }
    CHECK(envelope.getLevel() == 1.0);

    // Decay: the distance to the sustain level down 60 dB in 9600 samples.
    for (int i = 0; i < 9600; ++i)
        envelope.next();
    CHECK_THAT(envelope.getLevel() - 0.5, WithinRel(0.5 / 1000.0, 1e-6));
    for (int i = 0; i < 48000; ++i)
        envelope.next();
    CHECK(envelope.getLevel() == 0.5);

    // Release: 60 dB in 14400 samples, then it ends below -100 dB.
    envelope.noteOff();
    for (int i = 0; i < 14400; ++i)
        envelope.next();
    CHECK_THAT(envelope.getLevel(), WithinRel(0.5 / 1000.0, 1e-6));
    CHECK(envelope.isActive());
    for (int i = 0; i < 48000 && envelope.isActive(); ++i)
        envelope.next();
    CHECK_FALSE(envelope.isActive());
    CHECK(envelope.getLevel() == 0.0);

    // Struck again while releasing: the attack goes on from where the level is.
    envelope.noteOn();
    envelope.next();
    CHECK_THAT(envelope.getLevel(), WithinAbs(1.0 / 4800.0, 1e-12));
}

TEST_CASE("Every key plays at its pitch", "[engine][synth]")
{
    const double rate = GENERATE(44100.0, 48000.0, 96000.0);
    for (int key = 21; key <= 108; key += 3)
    {
        auto synth = plainSynth(rate, 0.0f); // sine
        const auto out = synth->run({midi::noteOn(0, key, 1.0f)}, static_cast<int>(rate * 0.6));
        const double measured =
            measuredFrequency(out, static_cast<std::size_t>(rate * 0.05), out.size(), rate);
        CAPTURE(rate, key, measured);
        CHECK(std::abs(cents(measured, frequencyOfKey(key))) < 0.1);
    }
}

TEST_CASE("Octave, semitone, fine tune and pitch bend move the pitch exactly", "[engine][synth]")
{
    auto synth = plainSynth(48000.0, 0.0f);
    synth->set("osc1Octave", 4.0f);    // +1
    synth->set("osc1Semitone", 19.0f); // +7
    synth->set("osc1Fine", 50.0f);
    synth->set("bendRange", 12.0f);
    auto out = synth->run({midi::noteOn(0, 57, 1.0f)}, 24000);
    CHECK(std::abs(cents(measuredFrequency(out, 2400, out.size(), 48000.0),
                         frequencyOfKey(57 + 12 + 7 + 0.5))) < 0.1);

    // A bend of half the way up: half the range.
    out = synth->run({midi::pitchBend(synth->position, 0.5f)}, 24000);
    CHECK(std::abs(cents(measuredFrequency(out, 2400, out.size(), 48000.0),
                         frequencyOfKey(57 + 12 + 7 + 0.5 + 6))) < 0.1);
}

TEST_CASE("The oscillators stay below the aliasing threshold up to C8", "[engine][synth]")
{
    const double rate = GENERATE(44100.0, 48000.0);
    const float wave = GENERATE(1.0f, 2.0f, 3.0f); // triangle, saw, pulse
    const float width = GENERATE(50.0f, 23.0f);
    if (wave != 3.0f && width != 50.0f)
        return;

    double worst = -1000.0;
    for (int key = 24; key <= 108; key += 7)
    {
        auto synth = plainSynth(rate, wave);
        synth->set("osc1Width", width);
        const auto out = synth->run({midi::noteOn(0, key, 1.0f)}, 70000);
        const std::vector<float> steady(out.begin() + 4000, out.end());
        const double spurious = daw::test::strongestNonHarmonicDecibels(steady, frequencyOfKey(key), rate);
        CAPTURE(rate, wave, width, key, spurious);
        CHECK(spurious < -80.0);
        worst = std::max(worst, spurious);
    }
    INFO("worst " << worst << " dB");
    SUCCEED();
}

TEST_CASE("The synth's output does not depend on the host's blocks", "[engine][synth]")
{
    const auto play = [](int blockSize) {
        InstrumentRunner synth(std::make_unique<Synth>(), 48000.0, 512);
        synth.set("lfoToCutoff", 24.0f);
        synth.set("lfoToPitch", 30.0f);
        synth.set("glide", 80.0f);
        synth.set("polyphony", 2.0f); // three voices: stealing happens
        synth.set("noiseLevel", 20.0f);
        std::vector<float> cutoff(40000);
        for (std::size_t i = 0; i < cutoff.size(); ++i)
            cutoff[i] = 0.3f + 0.5f * static_cast<float>(i) / static_cast<float>(cutoff.size());
        synth.automate("cutoff", cutoff);
        const std::vector<midi::Event> events{
            midi::noteOn(0, 60, 0.8f, 0, 1),     midi::noteOn(700, 64, 0.5f, 0, 2),
            midi::noteOn(1333, 67, 1.0f, 0, 3),  midi::noteOn(2000, 71, 0.7f, 0, 4),
            midi::controller(2500, 1, 0.8f),     midi::pitchBend(3001, -0.3f),
            midi::controller(4000, 64, 1.0f),    midi::noteOff(5000, 60, 0, 1),
            midi::noteOff(6000, 71, 0, 4),       midi::controller(9000, 64, 0.0f),
            midi::noteOff(12000, 67, 0, 3),      midi::noteOff(13000, 64, 0, 2)};
        auto out = synth.run(events, 36000, blockSize);
        CHECK(synth.heapOperations == 0);
        return out;
    };

    const auto reference = play(512);
    for (const int blockSize : {1, 7, 16, 100, 333})
    {
        const auto out = play(blockSize);
        bool identical = true;
        for (std::size_t i = 0; i < reference.size(); ++i)
            identical = identical && std::bit_cast<std::uint32_t>(out[i]) ==
                                         std::bit_cast<std::uint32_t>(reference[i]);
        CAPTURE(blockSize);
        CHECK(identical);
    }
}

namespace
{

// The amplitude of the component at frequency over [from, to), Hann windowed.
double amplitudeAt(const std::vector<float>& samples, std::size_t from, std::size_t to, double frequency,
                   double rate)
{
    double re = 0.0;
    double im = 0.0;
    double weights = 0.0;
    const double count = static_cast<double>(to - from);
    for (std::size_t i = from; i < to; ++i)
    {
        const double n = static_cast<double>(i - from);
        const double w = 0.5 - 0.5 * std::cos(2.0 * std::numbers::pi * n / count);
        const double phase = 2.0 * std::numbers::pi * frequency * static_cast<double>(i) / rate;
        re += w * samples[i] * std::cos(phase);
        im -= w * samples[i] * std::sin(phase);
        weights += w;
    }
    return 2.0 * std::hypot(re, im) / weights;
}

// The loudest a single voice gets, at full velocity, over the keys from first to last.
float loudestKey(const std::function<void(InstrumentRunner&)>& patch, int first, int last)
{
    float loudest = 0.0f;
    for (int key = first; key <= last; ++key)
    {
        InstrumentRunner synth(std::make_unique<Synth>(), 48000.0, 512);
        patch(synth);
        // The filter envelope sweeps the cutoff through the harmonics in the first
        // tenths of a second.
        const auto out = synth.run({midi::noteOn(0, key, 1.0f, 0, 1)}, 12000, 512);
        for (const auto value : out)
            loudest = std::max(loudest, std::abs(value));
    }
    return loudest;
}

} // namespace

TEST_CASE("Full resonance keeps the level in check on every key", "[engine][synth]")
{
    // The default sound (two saws at 70 %, the cutoff following keys, velocity and
    // the filter envelope's +24 semitones, level -6 dB) and a louder one (both
    // oscillators full and some noise), with the resonance at 100 %: Q 20 boosts
    // what sits at the cutoff by 26 dB. Without resonance the loudest key of the
    // default sound peaks at 0.65 to 0.98 depending on the filter; a single voice at
    // full resonance must stay under 0 dBFS, the louder sound under +3 dB.
    const auto filterType = GENERATE(0.0f, 1.0f, 2.0f, 3.0f); // LP12, LP24, BP, HP
    const auto cutoff = GENERATE(300.0f, 2000.0f);
    CAPTURE(filterType, cutoff);

    const auto resonant = [&](InstrumentRunner& synth) {
        synth.set("filterType", filterType);
        synth.set("cutoff", cutoff);
        synth.set("resonance", 100.0f);
    };
    CHECK(loudestKey(resonant, 0, 127) <= 1.0f);

    const auto loud = [&](InstrumentRunner& synth) {
        resonant(synth);
        synth.set("osc1Level", 100.0f);
        synth.set("osc2Level", 100.0f);
        synth.set("noiseLevel", 30.0f);
    };
    CHECK(loudestKey(loud, 0, 127) <= 1.4142f);
}

TEST_CASE("Full resonance still rings at the cutoff", "[engine][synth]")
{
    // C1 (65.4 Hz) on one saw, the cutoff fixed on a harmonic: at full resonance that
    // harmonic stands out above the fundamental by far more than without, whether
    // it is quiet enough to stay linear (the 16th) or loud enough to saturate (the
    // 2nd).
    const double fundamental = frequencyOfKey(24.0);
    for (const int harmonic : {16, 2})
    {
        const auto emphasis = [&](float resonance) {
            auto synth = plainSynth(48000.0, 2.0f); // a saw
            synth->set("cutoff", static_cast<float>(fundamental * harmonic));
            synth->set("resonance", resonance);
            const auto out = synth->run({midi::noteOn(0, 24, 1.0f, 0, 1)}, 72000, 512);
            return 20.0 * std::log10(amplitudeAt(out, 24000, 72000, fundamental * harmonic, 48000.0) /
                                     amplitudeAt(out, 24000, 72000, fundamental, 48000.0));
        };
        CAPTURE(harmonic);
        const double gained = emphasis(100.0f) - emphasis(0.0f);
        CAPTURE(gained);
        // A linear filter gives 32.0 and 27.6 dB here (Q 20 against Q 0.5); the
        // saturation leaves the quiet 16th 29.6 and holds the loud 2nd at 15.9.
        CHECK(gained >= (harmonic == 16 ? 28.0 : 14.0));
    }
}

TEST_CASE("Voices are taken without allocating, stolen when they run out", "[engine][synth]")
{
    InstrumentRunner synth(std::make_unique<Synth>(), 48000.0, 512);
    synth.set("polyphony", 3.0f); // four voices
    synth.set("ampRelease", 2000.0f);
    auto& instrument = dynamic_cast<Synth&>(*synth.effect);

    std::vector<midi::Event> events;
    for (int i = 0; i < 64; ++i)
        events.push_back(midi::noteOn(i * 10, 30 + i, 0.5f, 0, i + 1));
    synth.run(events, 2000);
    CHECK(synth.heapOperations == 0);
    // Four voices and the spare ones fading out stolen notes (3 ms).
    CHECK(instrument.getActiveVoices() <= Synth::maximumVoices + Synth::spareVoices);
    synth.run({}, 1000);
    CHECK(instrument.getActiveVoices() == 4);

    // All 32 at once, and many more.
    InstrumentRunner big(std::make_unique<Synth>(), 48000.0, 512);
    big.set("polyphony", 31.0f);
    events.clear();
    for (int i = 0; i < 200; ++i)
        events.push_back(midi::noteOn(i % 50, i % 128, 0.5f, i / 128, i + 1));
    for (int i = 0; i < 200; ++i)
        events.push_back(midi::noteOff(3000 + i, i % 128, i / 128, i + 1));
    big.run(events, 48000);
    CHECK(big.heapOperations == 0);
    CHECK(dynamic_cast<Synth&>(*big.effect).getActiveVoices() == 0);
}

TEST_CASE("The sustain pedal holds released notes until it goes up", "[engine][synth]")
{
    auto synth = plainSynth(48000.0, 0.0f);
    synth->set("ampRelease", 1.0f);
    auto& instrument = dynamic_cast<Synth&>(*synth->effect);

    synth->run({midi::controller(0, 64, 1.0f), midi::noteOn(10, 60, 1.0f), midi::noteOff(1000, 60)},
               6000);
    CHECK(instrument.getActiveVoices() == 1);
    synth->run({midi::controller(6000, 64, 0.0f)}, 6000);
    CHECK(instrument.getActiveVoices() == 0);
}

TEST_CASE("A key struck again takes its own voice; glide moves from the last note",
          "[engine][synth]")
{
    auto synth = plainSynth(48000.0, 0.0f);
    auto& instrument = dynamic_cast<Synth&>(*synth->effect);
    synth->run({midi::noteOn(0, 60, 1.0f, 0, 1), midi::noteOn(100, 60, 1.0f, 0, 2)}, 1000);
    CHECK(instrument.getActiveVoices() == 1);

    // Glide of 200 ms from 60 to 72: halfway at 100 ms, there at 200 ms.
    auto glider = plainSynth(48000.0, 0.0f);
    glider->set("glide", 200.0f);
    glider->set("voiceMode", 2.0f); // legato
    glider->run({midi::noteOn(0, 60, 1.0f)}, 4800);
    const auto out = glider->run({midi::noteOn(4800, 72, 1.0f)}, 48000);
    CHECK(std::abs(cents(measuredFrequency(out, 4400, 5200, 48000.0), frequencyOfKey(66))) < 25.0);
    CHECK(std::abs(cents(measuredFrequency(out, 12000, 40000, 48000.0), frequencyOfKey(72))) < 0.1);
    CHECK(instrument.getActiveVoices() == 1);
}

TEST_CASE("The aliasing measurement sees the aliasing of a naive saw", "[engine][synth]")
{
    // A saw computed sample by sample, not band-limited: its harmonics above Nyquist
    // fold back, well above any threshold.
    constexpr double rate = 48000.0;
    const double frequency = frequencyOfKey(101);
    std::vector<float> naive(70000);
    double phase = 0.0;
    for (auto& sample : naive)
    {
        sample = static_cast<float>(2.0 * phase - 1.0);
        phase += frequency / rate;
        phase -= std::floor(phase);
    }
    CHECK(daw::test::strongestNonHarmonicDecibels(naive, frequency, rate) > -40.0);
}

TEST_CASE("The export equals real-time playback with the synth", "[engine][synth][bounce]")
{
    const auto makeSong = [](int maximumBlock) {
        daw::engine::fx::ProcessSetup setup;
        setup.sampleRate = 48000.0;
        setup.maximumBlockSize = maximumBlock;
        auto song = std::make_unique<daw::engine::Arrangement>();
        song->sampleRate = 48000.0;
        song->length = 40000;
        daw::engine::ArrangementTrack track;
        track.id = 1;
        track.playsNotes = true;
        for (int i = 0; i < 40; ++i)
            track.notes.push_back({500 + i * 800, 500 + i * 800 + 300 + (i * 131) % 3000,
                                   static_cast<std::uint8_t>(36 + (i * 5) % 48), 0,
                                   0.3f + 0.015f * static_cast<float>(i), static_cast<std::uint64_t>(i + 1)});
        daw::engine::ControllerLane wheel;
        wheel.number = 1;
        wheel.steps = {{10000, 0.7f}, {25000, 0.1f}};
        track.controllers = {wheel};
        daw::engine::ChainSlot slot;
        slot.instance = std::make_shared<daw::engine::fx::EffectInstance>(std::make_unique<Synth>(), setup);
        for (const auto& parameter : slot.instance->getDescription().parameters)
            slot.parameters.push_back(parameter.toNormalised(parameter.defaultValue));
        slot.parameters[Synth::lfoToCutoff] = 0.5f;
        slot.automation.resize(Synth::numParameters);
        slot.automation[Synth::cutoff] = std::make_shared<const daw::engine::AutomationCurve>(
            std::vector<daw::engine::AutomationPoint>{{0, 0.4f, 0.0f}, {30000, 0.8f, 0.4f}});
        track.inserts.slots.push_back(std::move(slot));
        song->tracks.push_back(std::move(track));
        return song;
    };

    const auto song = makeSong(daw::engine::OfflineRenderer::defaultBlockSize);
    daw::engine::OfflineRenderer renderer(*song);
    juce::AudioBuffer<float> exported(2, 40000);
    renderer.render(exported);
    CHECK(exported.getMagnitude(0, 0, 40000) > 0.05f);

    const int blockSize = GENERATE(64, 441);
    daw::engine::Engine engine;
    engine.prepare(48000.0, blockSize);
    engine.setArrangement(makeSong(blockSize));
    engine.play();

    std::vector<float> left;
    juce::AudioBuffer<float> block(2, blockSize);
    std::size_t heapOperations = 0;
    while (left.size() < 40000)
    {
        heapOperations += daw::test::heapOperationsDuring([&] { engine.process(block); });
        left.insert(left.end(), block.getReadPointer(0), block.getReadPointer(0) + blockSize);
    }

    bool identical = true;
    for (int k = 0; k < 40000; ++k)
        identical = identical && std::bit_cast<std::uint32_t>(left[static_cast<std::size_t>(k)]) ==
                                     std::bit_cast<std::uint32_t>(exported.getSample(0, k));
    CAPTURE(blockSize);
    CHECK(identical);
    CHECK(heapOperations == 0);
    engine.collectGarbage();
}
