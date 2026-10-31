#include "daw/engine/dsp/LatencyProbe.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_string.hpp>

#include <array>
#include <cmath>
#include <random>
#include <vector>

using Catch::Matchers::ContainsSubstring;
using daw::engine::dsp::LatencyProbe;

namespace
{

constexpr double sampleRate = 48000.0;

// What an input would deliver: the signal delayed by delay[burst] samples, scaled
// by gain, plus noise of the given amplitude.
std::vector<float> loopBack(const LatencyProbe& probe, std::array<int, LatencyProbe::numBursts> delays,
                            float gain, float noise)
{
    const auto& signal = probe.getSignal();
    std::vector<float> captured(signal.size(), 0.0f);
    const auto period = signal.size() / LatencyProbe::numBursts;

    for (std::size_t burst = 0; burst < LatencyProbe::numBursts; ++burst)
        for (std::size_t i = 0; i < LatencyProbe::burstLength; ++i)
        {
            const auto target = burst * period + i + static_cast<std::size_t>(delays[burst]);
            if (target < captured.size())
                captured[target] += gain * signal[burst * period + i];
        }

    std::mt19937 random(7);
    std::uniform_real_distribution<float> uniform(-noise, noise);
    for (auto& sample : captured)
        sample += uniform(random);
    return captured;
}

} // namespace

TEST_CASE("The latency probe finds a known delay to the sample, in noise and upside down",
          "[engine][latency]")
{
    const LatencyProbe probe(sampleRate);
    REQUIRE(probe.getMaximumLatency() == 24000);

    const int delay = GENERATE(0, 1, 129, 4096, 23999);
    const float gain = GENERATE(0.5f, -0.02f);
    CAPTURE(delay, gain);

    // Noise at the level of the returning signal: a poor connection still measures.
    const auto captured = loopBack(probe, {delay, delay, delay, delay, delay}, gain,
                                   std::abs(gain) * 0.25f);
    const auto measurement = probe.analyse(captured.data(), static_cast<int>(captured.size()));

    CAPTURE(measurement.problem, measurement.clarity);
    REQUIRE(measurement.found);
    CHECK(measurement.latency == delay);
    CHECK(measurement.inverted == (gain < 0.0f));
    CHECK(measurement.clarity > 3.0);
}

TEST_CASE("Without a clear, steady return the latency probe reports why", "[engine][latency]")
{
    const LatencyProbe probe(sampleRate);
    const auto size = static_cast<int>(probe.getSignal().size());

    const std::vector<float> silence(probe.getSignal().size(), 0.0f);
    CHECK_THAT(probe.analyse(silence.data(), size).problem.toStdString(),
               ContainsSubstring("did not come back"));

    const auto noise = loopBack(probe, {0, 0, 0, 0, 0}, 0.0f, 0.1f);
    CHECK_FALSE(probe.analyse(noise.data(), size).found);

    const auto wandering = loopBack(probe, {100, 100, 105, 100, 90}, 0.5f, 0.0f);
    const auto measurement = probe.analyse(wandering.data(), size);
    CHECK_FALSE(measurement.found);
    CHECK_THAT(measurement.problem.toStdString(), ContainsSubstring("different delays"));

    CHECK_FALSE(probe.analyse(silence.data(), size - 1).found);
}
