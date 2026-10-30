#include "daw/engine/Engine.h"
#include "daw/engine/dsp/MeterBallistics.h"
#include "daw/engine/dsp/PeakLevelMeter.h"

#include "support/RealtimeCheck.h"
#include "support/SignalAnalysis.h"
#include "support/TestArrangements.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <memory>
#include <thread>

using Catch::Matchers::WithinAbs;
using daw::engine::dsp::MeterBallistics;
using daw::engine::dsp::PeakLevelMeter;

namespace
{
float readingInDb(float peak)
{
    MeterBallistics meter;
    meter.update(peak, 0.0);
    return meter.getLevelDb();
}
} // namespace

TEST_CASE("The peak meter reads sine levels in dBFS", "[engine][meter]")
{
    // 1 kHz at 48 kHz puts a sample exactly on each crest.
    const double amplitudeDb = GENERATE(0.0, -6.0, -20.0, -40.0);
    const double amplitude = std::pow(10.0, amplitudeDb / 20.0);
    const auto sine = daw::test::makeSine(2, 4800, 1000.0, 48000.0, amplitude);

    PeakLevelMeter meter;
    meter.process(sine, 0, sine.getNumSamples());

    const float left = readingInDb(meter.takePeak(0));
    const float right = readingInDb(meter.takePeak(1));

    CAPTURE(amplitudeDb, left, right);
    CHECK_THAT(left, WithinAbs(amplitudeDb, 0.01));
    CHECK_THAT(right, WithinAbs(amplitudeDb, 0.01));
}

TEST_CASE("A sine with an amplitude of 0.5 reads -6.02 dB", "[engine][meter]")
{
    const auto sine = daw::test::makeSine(1, 4800, 1000.0, 48000.0, 0.5);
    PeakLevelMeter meter;
    meter.process(sine, 0, sine.getNumSamples());

    CHECK_THAT(readingInDb(meter.takePeak(0)), WithinAbs(-6.0206, 0.001));
}

TEST_CASE("Taking the peak resets it", "[engine][meter]")
{
    auto loud = daw::test::makeSine(1, 480, 1000.0, 48000.0, 0.8);
    PeakLevelMeter meter;
    meter.process(loud, 0, loud.getNumSamples());

    CHECK_THAT(meter.takePeak(0), WithinAbs(0.8, 1.0e-6));
    CHECK(meter.takePeak(0) == 0.0f);
    CHECK(meter.takePeak(5) == 0.0f); // unmeasured channel
}

TEST_CASE("The peak is the highest block since the last reading", "[engine][meter]")
{
    PeakLevelMeter meter;
    const auto loud = daw::test::makeSine(1, 480, 1000.0, 48000.0, 0.9);
    const auto quiet = daw::test::makeSine(1, 480, 1000.0, 48000.0, 0.1);

    meter.process(quiet, 0, 480);
    meter.process(loud, 0, 480);
    meter.process(quiet, 0, 480);

    CHECK_THAT(meter.takePeak(0), WithinAbs(0.9, 1.0e-6));
}

TEST_CASE("The peak meter is real-time safe and never loses a peak to the reader",
          "[engine][meter]")
{
    PeakLevelMeter meter;
    juce::AudioBuffer<float> block(2, 64);
    block.clear();

    CHECK(daw::test::heapOperationsDuring([&] { meter.process(block, 0, 64); }) == 0);

    // The audio thread writes rising peaks while the reader keeps taking them: the
    // largest value must always reach the reader.
    std::atomic<bool> done{false};
    float largestSeen = 0.0f;
    std::thread reader([&] {
        while (!done.load())
            largestSeen = std::max(largestSeen, meter.takePeak(0));
        largestSeen = std::max(largestSeen, meter.takePeak(0));
    });

    for (int i = 1; i <= 20000; ++i)
    {
        block.setSample(0, 0, static_cast<float>(i) / 20000.0f);
        meter.process(block, 0, 64);
    }
    done.store(true);
    reader.join();

    CHECK(largestSeen == 1.0f);
}

TEST_CASE("The meter falls at 20 dB per second whatever the refresh rate", "[engine][meter]")
{
    const double refreshRate = GENERATE(15.0, 30.0, 60.0, 144.0);

    MeterBallistics meter;
    meter.update(1.0f, 0.0); // 0 dBFS
    const int updates = static_cast<int>(refreshRate); // one second of silence
    for (int i = 0; i < updates; ++i)
        meter.update(0.0f, 1.0 / refreshRate);

    CAPTURE(refreshRate);
    CHECK_THAT(meter.getLevelDb(), WithinAbs(-20.0, 1.0e-3));
}

TEST_CASE("The meter rises instantly and bottoms out at the floor", "[engine][meter]")
{
    MeterBallistics meter;
    CHECK(meter.getLevelDb() == MeterBallistics::floorDb);

    meter.update(0.5f, 1.0 / 60.0);
    CHECK_THAT(meter.getLevelDb(), WithinAbs(-6.0206, 0.001));

    for (int i = 0; i < 600; ++i)
        meter.update(0.0f, 1.0 / 60.0);
    CHECK(meter.getLevelDb() == MeterBallistics::floorDb);
    CHECK(MeterBallistics::toProportion(meter.getLevelDb()) == 0.0f);
    CHECK(MeterBallistics::toProportion(0.0f) == 1.0f);
    CHECK_THAT(MeterBallistics::toProportion(-30.0f), WithinAbs(0.5, 1.0e-6));
}

TEST_CASE("The peak hold stays for 1.5 s, then falls, whatever the refresh rate",
          "[engine][meter]")
{
    const double refreshRate = GENERATE(30.0, 60.0);
    const double step = 1.0 / refreshRate;

    MeterBallistics meter;
    meter.update(juce::Decibels::decibelsToGain(-10.0f), 0.0);

    // 1.4 s later the hold is still at -10 dB, while the level has fallen.
    for (int i = 0; i < static_cast<int>(1.4 * refreshRate); ++i)
        meter.update(0.0f, step);
    CHECK_THAT(meter.getPeakHoldDb(), WithinAbs(-10.0, 1.0e-4));
    CHECK(meter.getLevelDb() < -30.0f);

    // At 2.0 s it has been falling for 0.5 s: 10 dB lower.
    for (int i = 0; i < static_cast<int>(0.6 * refreshRate); ++i)
        meter.update(0.0f, step);

    CAPTURE(refreshRate);
    CHECK_THAT(meter.getPeakHoldDb(), WithinAbs(-20.0, 1.0e-3));
}

TEST_CASE("The clip indicator latches at 0 dBFS until reset", "[engine][meter]")
{
    MeterBallistics meter;
    meter.update(0.99f, 0.1);
    CHECK_FALSE(meter.isClipping());

    meter.update(1.0f, 0.1);
    CHECK(meter.isClipping());

    meter.update(0.0f, 10.0);
    CHECK(meter.isClipping());

    meter.resetClip();
    CHECK_FALSE(meter.isClipping());
}

TEST_CASE("The engine meters what it plays", "[engine][meter]")
{
    daw::engine::Engine engine;
    engine.prepare(48000.0, 480);

    const auto source =
        daw::test::makeSource(daw::test::makeSine(2, 48000, 1000.0, 48000.0, 0.5), 48000.0);
    engine.setArrangement(daw::test::makeWholeFileArrangement(source));
    engine.play();

    juce::AudioBuffer<float> block(2, 480);
    for (int i = 0; i < 10; ++i)
        engine.process(block);

    CHECK_THAT(engine.takeOutputPeak(0), WithinAbs(0.5, 1.0e-6));
    CHECK_THAT(engine.takeOutputPeak(1), WithinAbs(0.5, 1.0e-6));

    engine.pause();
    for (int i = 0; i < 10; ++i)
        engine.process(block);
    (void) engine.takeOutputPeak(0); // includes the fade-out

    engine.process(block);
    CHECK(engine.takeOutputPeak(0) == 0.0f);
}
