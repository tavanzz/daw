#include "daw/engine/fx/Reverb.h"

#include "support/EffectRunner.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <memory>
#include <random>
#include <vector>

using Catch::Matchers::WithinRel;
using daw::engine::fx::Reverb;
using daw::test::EffectRunner;

namespace
{

constexpr double sampleRate = 48000.0;

// Wet only, no pre-delay, nothing cutting or damping: the decay as set.
std::unique_ptr<EffectRunner> plainReverb(float decay, float size, float damping = 0.0f)
{
    auto runner = std::make_unique<EffectRunner>(std::make_unique<Reverb>(), sampleRate);
    runner->set("decay", decay);
    runner->set("size", size);
    runner->set("damping", damping);
    runner->set("preDelay", 0.0f);
    runner->set("lowCut", 20.0f);
    runner->set("highCut", 20000.0f);
    runner->set("mix", 100.0f);
    return runner;
}

juce::AudioBuffer<float> impulse(int length)
{
    juce::AudioBuffer<float> buffer(2, length);
    buffer.clear();
    buffer.setSample(0, 0, 1.0f);
    buffer.setSample(1, 0, 1.0f);
    return buffer;
}

// The energy decay curve (Schroeder's backward integration), in dB, per sample.
std::vector<double> energyDecay(const juce::AudioBuffer<float>& response)
{
    std::vector<double> curve(static_cast<std::size_t>(response.getNumSamples()));
    double remaining = 0.0;
    for (int i = response.getNumSamples() - 1; i >= 0; --i)
    {
        const double left = response.getSample(0, i);
        const double right = response.getSample(1, i);
        remaining += left * left + right * right;
        curve[static_cast<std::size_t>(i)] = remaining;
    }
    const double total = curve.front();
    for (auto& value : curve)
        value = 10.0 * std::log10(std::max(value / total, 1e-30));
    return curve;
}

double timeOf(const std::vector<double>& curve, double decibels)
{
    for (std::size_t i = 0; i < curve.size(); ++i)
        if (curve[i] <= decibels)
            return static_cast<double>(i) / sampleRate;
    return static_cast<double>(curve.size()) / sampleRate;
}

// T30 extrapolated to 60 dB.
double decayTime(const std::vector<double>& curve)
{
    return 2.0 * (timeOf(curve, -35.0) - timeOf(curve, -5.0));
}

float valueOf(EffectRunner& runner, const char* id)
{
    return runner.values()[static_cast<std::size_t>(runner.instance.getDescription().indexOf(id))];
}

} // namespace

TEST_CASE("The reverb's impulse response decays in the decay time", "[engine][fx][reverb]")
{
    const float decay = GENERATE(0.5f, 2.0f, 6.0f);
    const float size = GENERATE(30.0f, 100.0f);

    auto runner = plainReverb(decay, size);
    const auto length = static_cast<int>((2.0 * decay + 0.5) * sampleRate);
    const auto response = runner->run(impulse(length));
    const double set = valueOf(*runner, "decay");
    const auto curve = energyDecay(response);

    CAPTURE(set, size);
    CHECK_THAT(decayTime(curve), WithinRel(set, 0.1));
    // 60 dB down within the decay time (and a little for the sound to build up).
    CHECK(curve[static_cast<std::size_t>((set * 1.1 + 0.05) * sampleRate)] < -60.0);
    CHECK(runner->heapOperations == 0);
    CHECK(runner->instance.getLatency() == 0);
}

TEST_CASE("Damping shortens the decay, it never lengthens it", "[engine][fx][reverb]")
{
    auto runner = plainReverb(3.0f, 70.0f, 100.0f);
    const auto response = runner->run(impulse(static_cast<int>(7.0 * sampleRate)));
    const double set = valueOf(*runner, "decay");
    const auto curve = energyDecay(response);
    CHECK(decayTime(curve) < set * 1.05);
    CHECK(curve[static_cast<std::size_t>((set * 1.1 + 0.05) * sampleRate)] < -60.0);
}

TEST_CASE("The reverb stays stable whatever its settings: never NaN or infinity",
          "[engine][fx][reverb]")
{
    std::mt19937 random(20261103);
    std::uniform_real_distribution<float> unit(0.0f, 1.0f);

    juce::AudioBuffer<float> noise(2, 24000);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < noise.getNumSamples(); ++i)
            noise.setSample(channel, i, daw::test::testInputSample(channel, i));
    const juce::AudioBuffer<float> silence = [] {
        juce::AudioBuffer<float> buffer(2, 48000);
        buffer.clear();
        return buffer;
    }();

    bool finite = true;
    float loudest = 0.0f;
    std::size_t heapOperations = 0;
    for (int round = 0; round < 24; ++round)
    {
        EffectRunner runner(std::make_unique<Reverb>(), sampleRate);
        const auto& parameters = runner.instance.getDescription().parameters;
        const auto randomise = [&] {
            for (std::size_t p = 0; p < parameters.size(); ++p)
                runner.targets[p] = round % 3 == 0 ? (unit(random) < 0.5f ? 0.0f : 1.0f) : unit(random);
        };

        randomise();
        auto loud = runner.run(noise, 333);
        randomise(); // new settings while ringing: sizes glide, decays change
        auto after = runner.run(silence, 64);

        for (const auto* buffer : {&loud, &after})
            for (int channel = 0; channel < 2; ++channel)
                for (int i = 0; i < buffer->getNumSamples(); ++i)
                {
                    const float value = buffer->getSample(channel, i);
                    finite = finite && std::isfinite(value);
                    loudest = std::max(loudest, std::abs(value));
                }
        heapOperations += runner.heapOperations;
    }

    // The longest decay on full-scale noise builds up, but stays within bounds.
    auto longest = plainReverb(20.0f, 100.0f);
    juce::AudioBuffer<float> long_noise(2, 10 * 48000);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < long_noise.getNumSamples(); ++i)
            long_noise.setSample(channel, i, daw::test::testInputSample(channel + 2, i));
    const auto built = longest->run(long_noise);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < built.getNumSamples(); ++i)
            finite = finite && std::isfinite(built.getSample(channel, i));

    CHECK(finite);
    CHECK(loudest < 20.0f);
    CHECK(built.getMagnitude(0, built.getNumSamples()) < 30.0f);
    CHECK(heapOperations == 0);
}

TEST_CASE("A reverb in silence settles to exact zero, without denormals", "[engine][fx][reverb]")
{
    // As on the audio thread.
    const juce::ScopedNoDenormals noDenormals;

    auto runner = plainReverb(0.2f, 50.0f, 50.0f);
    const auto response = runner->run(impulse(30 * 48000), 512);

    bool noDenormal = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < response.getNumSamples(); ++i)
            noDenormal = noDenormal && std::fpclassify(response.getSample(channel, i)) != FP_SUBNORMAL;
    CHECK(noDenormal);
    CHECK(response.getMagnitude(29 * 48000, 48000) == 0.0f);
    CHECK(response.getMagnitude(0, 4800) > 0.0f);
}

TEST_CASE("Mix and width: dry when off, mono when narrow", "[engine][fx][reverb]")
{
    juce::AudioBuffer<float> input(2, 20000);
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 20000; ++i)
            input.setSample(channel, i, 0.5f * daw::test::testInputSample(channel, i));

    EffectRunner dry(std::make_unique<Reverb>(), sampleRate);
    dry.set("mix", 0.0f);
    const auto unchanged = dry.run(input);
    bool same = true;
    for (int channel = 0; channel < 2; ++channel)
        for (int i = 0; i < 20000; ++i)
            same = same && unchanged.getSample(channel, i) == input.getSample(channel, i);
    CHECK(same);

    auto narrow = plainReverb(1.0f, 60.0f);
    narrow->set("width", 0.0f);
    const auto mono = narrow->run(input);
    bool equal = true;
    for (int i = 0; i < 20000; ++i)
        equal = equal && mono.getSample(0, i) == mono.getSample(1, i);
    CHECK(equal);
    CHECK(mono.getMagnitude(0, 20000) > 0.0f);

    std::vector<float> values(8, 0.0f);
    values[Reverb::preDelay] = 50.0f;
    values[Reverb::decay] = 3.0f;
    CHECK_THAT(Reverb().getTailSeconds(values), WithinRel(0.05 + 5.0, 1e-6));
}
