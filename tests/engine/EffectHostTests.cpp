#include "daw/engine/dsp/DelayLine.h"
#include "daw/engine/fx/EffectInstance.h"

#include "support/RealtimeCheck.h"
#include "support/TestEffects.h"

#include <catch2/catch_test_macros.hpp>
#include <catch2/generators/catch_generators.hpp>
#include <catch2/matchers/catch_matchers_floating_point.hpp>

#include <cmath>
#include <limits>
#include <memory>
#include <vector>

using Catch::Matchers::WithinAbs;
using Catch::Matchers::WithinRel;
using daw::engine::dsp::DelayLine;
using daw::engine::fx::EffectInstance;
using daw::engine::fx::EffectScratch;
using daw::engine::fx::ParameterInfo;
using daw::engine::fx::ParameterMapping;
using daw::engine::fx::ParameterScale;
using daw::engine::fx::ProcessSetup;
using daw::engine::fx::SlotContext;
using daw::engine::fx::SlotSettings;
using daw::test::BrokenEffect;
using daw::test::GainEffect;
using daw::test::LatencyEffect;
using daw::test::ParameterProbe;

namespace
{

constexpr double sampleRate = 48000.0;
constexpr int fadeLength = 480; // 10 ms
constexpr int rampLength = 960; // 20 ms

ProcessSetup setupWithBlock(int blockSize)
{
    ProcessSetup setup;
    setup.sampleRate = sampleRate;
    setup.maximumBlockSize = blockSize;
    return setup;
}

// Runs a stereo signal through an instance block by block; both channels get the
// same input (the right one negated, so that channel mix-ups show).
struct Runner
{
    explicit Runner(std::unique_ptr<daw::engine::fx::Effect> effect, bool fadeIn = false,
                    int blockSize = 512)
        : instance(std::move(effect), setupWithBlock(blockSize), fadeIn)
    {
        scratch.prepare(blockSize);
    }

    std::vector<float> run(const std::vector<float>& input, int blockSize = 256)
    {
        std::vector<float> left = input;
        std::vector<float> right(input.size());
        for (std::size_t i = 0; i < input.size(); ++i)
            right[i] = -input[i];

        heapOperations += daw::test::heapOperationsDuring([&] {
            for (std::size_t done = 0; done < input.size();)
            {
                const auto piece = std::min(static_cast<std::size_t>(blockSize), input.size() - done);
                float* const channels[] = {left.data() + done, right.data() + done};
                instance.process(channels, static_cast<int>(piece), settings, context, scratch);
                done += piece;
            }
        });

        for (std::size_t i = 0; i < input.size(); ++i)
            rightMatches = rightMatches && right[i] == -left[i];
        return left;
    }

    EffectInstance instance;
    EffectScratch scratch;
    std::vector<float> targets;
    SlotSettings settings;
    SlotContext context;
    std::size_t heapOperations = 0;
    bool rightMatches = true;

    void setTargets(std::vector<float> normalised)
    {
        targets = std::move(normalised);
        settings.parameters = targets;
    }
};

std::vector<float> numbered(std::size_t length)
{
    std::vector<float> samples(length);
    for (std::size_t i = 0; i < length; ++i)
        samples[i] = static_cast<float>(i + 1) * 0.001f;
    return samples;
}

float largestStep(const std::vector<float>& samples, std::size_t from, std::size_t to)
{
    float largest = 0.0f;
    for (std::size_t i = std::max<std::size_t>(from, 1); i < to; ++i)
        largest = std::max(largest, std::abs(samples[i] - samples[i - 1]));
    return largest;
}

} // namespace

TEST_CASE("Parameters spread their range over 0 to 1 by their scale", "[engine][fx]")
{
    ParameterInfo level;
    level.minimum = -60.0f;
    level.maximum = 0.0f;
    CHECK(level.toNormalised(-60.0f) == 0.0f);
    CHECK(level.toNormalised(-15.0f) == 0.75f);
    CHECK(level.fromNormalised(0.5f) == -30.0f);
    CHECK(level.fromNormalised(2.0f) == 0.0f);
    CHECK(level.clamp(10.0f) == 0.0f);

    ParameterInfo frequency;
    frequency.minimum = 20.0f;
    frequency.maximum = 20000.0f;
    frequency.scale = ParameterScale::logarithmic;
    CHECK(frequency.fromNormalised(0.0f) == 20.0f);
    CHECK(frequency.fromNormalised(1.0f) == 20000.0f);
    CHECK_THAT(frequency.fromNormalised(0.5f), WithinRel(std::sqrt(20.0 * 20000.0), 1e-5));
    CHECK_THAT(frequency.fromNormalised(frequency.toNormalised(1234.5f)), WithinRel(1234.5, 1e-5));

    const auto shape = daw::engine::fx::choiceParameter("shape", "Shape", {"A", "B", "C"}, 1);
    CHECK(shape.defaultValue == 1.0f);
    CHECK(shape.toNormalised(2.0f) == 1.0f);
    CHECK(shape.toNormalised(1.0f) == 0.5f);
    CHECK(shape.fromNormalised(0.3f) == 1.0f);
    CHECK(shape.clamp(1.4f) == 1.0f);
    CHECK(shape.toText(2.0f) == "C");

    const auto toggle = daw::engine::fx::switchParameter("on", "On", true);
    CHECK(toggle.fromNormalised(1.0f) == 1.0f);
    CHECK(toggle.toText(0.0f) == "Off");

    frequency.unit = "Hz";
    CHECK(frequency.toText(1200.0f) == "1.20 kHz");
    CHECK(frequency.toText(440.0f) == "440 Hz");

    // NaN never gets through.
    CHECK(ParameterMapping(level).toValue(std::numeric_limits<float>::quiet_NaN()) == -60.0f);
    CHECK(level.clamp(std::numeric_limits<float>::quiet_NaN()) == level.defaultValue);
}

TEST_CASE("A delay line delays by whole samples, bit for bit", "[engine][fx]")
{
    const int delay = GENERATE(0, 1, 100, 1000);

    DelayLine line;
    line.prepare(2, 1000);
    line.setDelay(delay);

    const auto input = numbered(5000);
    std::vector<float> left = input;
    std::vector<float> right = input;
    const auto heapOperations = daw::test::heapOperationsDuring([&] {
        for (std::size_t done = 0; done < input.size(); done += 250)
        {
            float* const channels[] = {left.data() + done, right.data() + done};
            line.process(channels, 2, 250);
        }
    });

    CAPTURE(delay);
    CHECK(heapOperations == 0);
    bool exact = true;
    for (std::size_t i = 0; i < input.size(); ++i)
    {
        const float expected = static_cast<int>(i) < delay ? 0.0f : input[i - static_cast<std::size_t>(delay)];
        exact = exact && left[i] == expected && right[i] == expected;
    }
    CHECK(exact);
}

TEST_CASE("An effect switched off passes the audio bit for bit and is not processed",
          "[engine][fx]")
{
    auto effect = std::make_unique<GainEffect>(0.5f);
    auto* gain = effect.get();
    Runner runner(std::move(effect));
    runner.settings.enabled = false;

    const auto input = numbered(3000);
    const auto output = runner.run(input);

    CHECK(output == input);
    CHECK(runner.rightMatches);
    CHECK(gain->processed == 0);
    CHECK_FALSE(runner.instance.isRunning());
    CHECK(runner.heapOperations == 0);
}

TEST_CASE("An effect that is on gives its own output bit for bit", "[engine][fx]")
{
    Runner runner(std::make_unique<GainEffect>(0.5f));

    const auto input = numbered(3000);
    const auto output = runner.run(input);

    bool exact = true;
    for (std::size_t i = 0; i < input.size(); ++i)
        exact = exact && output[i] == input[i] * 0.5f;
    CHECK(exact);
    CHECK(runner.rightMatches);
    CHECK(runner.heapOperations == 0);
}

TEST_CASE("Bypassing an effect with latency never moves the audio in time", "[engine][fx]")
{
    Runner runner(std::make_unique<LatencyEffect>(100));
    CHECK(runner.instance.getLatency() == 100);

    // On, then off, then on again, while the audio runs: the output is always the
    // input 100 samples later, exactly (the dry signal is delayed to match).
    const auto input = numbered(6000);
    std::vector<float> output = runner.run({input.begin(), input.begin() + 2000});
    runner.settings.enabled = false;
    auto part = runner.run({input.begin() + 2000, input.begin() + 4000});
    output.insert(output.end(), part.begin(), part.end());
    runner.settings.enabled = true;
    part = runner.run({input.begin() + 4000, input.end()});
    output.insert(output.end(), part.begin(), part.end());

    bool exact = true;
    for (std::size_t i = 0; i < output.size(); ++i)
        exact = exact && output[i] == (i < 100 ? 0.0f : input[i - 100]);

    // Except right after switching back on: the effect restarts from silence, so
    // during the fade-in its part of the output is missing what it had not seen.
    bool exactOutsideFadeIn = true;
    for (std::size_t i = 0; i < output.size(); ++i)
        if (i < 4000 || i >= 4100 + fadeLength)
            exactOutsideFadeIn = exactOutsideFadeIn && output[i] == (i < 100 ? 0.0f : input[i - 100]);
    CHECK(exactOutsideFadeIn);
    CHECK_FALSE(exact);
    CHECK(runner.heapOperations == 0);
}

TEST_CASE("Switching an effect off and on crossfades over 10 ms", "[engine][fx]")
{
    auto effect = std::make_unique<GainEffect>(0.0f); // silence while on
    auto* gain = effect.get();
    Runner runner(std::move(effect));
    const std::vector<float> ones(4000, 1.0f);

    CHECK(runner.run(ones) == std::vector<float>(4000, 0.0f));

    runner.settings.enabled = false;
    const auto off = runner.run(ones);
    CHECK_THAT(off[0], WithinAbs(1.0 / fadeLength, 1e-6));
    CHECK_THAT(off[fadeLength / 2 - 1], WithinAbs(0.5, 1e-6));
    CHECK(off[fadeLength - 1] == 1.0f);
    CHECK(off.back() == 1.0f);
    CHECK(largestStep(off, 0, off.size()) <= 1.001f / fadeLength);

    // Off: no more processing.
    const auto processed = gain->processed;
    (void) runner.run(ones);
    CHECK(gain->processed == processed);
    CHECK_FALSE(runner.instance.isRunning());

    // On again: from a clean state, fading in.
    const auto resets = gain->resets;
    runner.settings.enabled = true;
    const auto on = runner.run(ones);
    CHECK(gain->resets == resets + 1);
    CHECK_THAT(on[0], WithinAbs(1.0 - 1.0 / fadeLength, 1e-6));
    CHECK(on[fadeLength - 1] == 0.0f);
    CHECK(on.back() == 0.0f);
    CHECK(largestStep(on, 0, on.size()) <= 1.001f / fadeLength);
    CHECK(runner.heapOperations == 0);
}

TEST_CASE("An effect added while playing fades in; one opened with the project does not",
          "[engine][fx]")
{
    const std::vector<float> ones(2000, 1.0f);

    Runner added(std::make_unique<GainEffect>(0.0f), true);
    const auto fadingIn = added.run(ones);
    CHECK_THAT(fadingIn[0], WithinAbs(1.0 - 1.0 / fadeLength, 1e-6));
    CHECK(fadingIn[fadeLength - 1] == 0.0f);
    CHECK(largestStep(fadingIn, 0, fadingIn.size()) <= 1.001f / fadeLength);

    Runner opened(std::make_unique<GainEffect>(0.0f));
    CHECK(opened.run(ones) == std::vector<float>(2000, 0.0f));

    // Opened bypassed: dry from the first sample.
    Runner bypassed(std::make_unique<GainEffect>(0.0f));
    bypassed.settings.enabled = false;
    CHECK(bypassed.run(ones) == ones);
}

TEST_CASE("Parameter changes ramp over 20 ms in the normalised domain; choices jump",
          "[engine][fx]")
{
    auto effect = std::make_unique<ParameterProbe>();
    auto* probe = effect.get();
    const auto description = probe->getDescription();
    Runner runner(std::move(effect));

    runner.setTargets({0.5f, 0.0f, 0.0f});
    (void) runner.run(std::vector<float>(1000, 0.0f));
    for (auto& values : probe->recorded)
        values.clear();

    // Level 50 -> 100, frequency 20 Hz -> 20 kHz, shape Sine -> Saw.
    runner.setTargets({1.0f, 1.0f, 1.0f});
    (void) runner.run(std::vector<float>(2000, 0.0f), 100);

    const auto& level = probe->recorded[0];
    const auto& frequency = probe->recorded[1];
    const auto& shape = probe->recorded[2];
    REQUIRE(level.size() == 2000);

    CHECK_THAT(level[0], WithinAbs(50.0 + 50.0 / rampLength, 1e-4));
    CHECK_THAT(level[rampLength / 2 - 1], WithinAbs(75.0, 1e-4));
    CHECK(level[rampLength - 1] == 100.0f);
    CHECK(level.back() == 100.0f);
    CHECK(largestStep(level, 0, level.size()) <= 50.0f / rampLength * 1.001f);

    // Halfway in the normalised domain is the geometric middle of the range.
    CHECK_THAT(frequency[rampLength / 2 - 1], WithinRel(std::sqrt(20.0 * 20000.0), 1e-4));
    CHECK(frequency[rampLength - 1] == 20000.0f);

    CHECK(shape.front() == 2.0f);
    CHECK(shape.back() == 2.0f);
    CHECK(runner.heapOperations == 0);
}

TEST_CASE("Automated parameters take the automation's values on every sample", "[engine][fx]")
{
    auto effect = std::make_unique<ParameterProbe>();
    auto* probe = effect.get();
    Runner runner(std::move(effect));
    runner.setTargets({0.5f, 0.5f, 0.0f});

    std::vector<float> lane(3000);
    for (std::size_t i = 0; i < lane.size(); ++i)
        lane[i] = static_cast<float>(i) / 3000.0f;
    std::vector<const float*> automation{lane.data(), nullptr, nullptr};

    // Runner hands the same automation pointer to every block: give each block its
    // own stretch of the lane.
    const auto info = probe->getDescription().parameters[0];
    std::size_t heapOperations = 0;
    for (std::size_t done = 0; done < lane.size(); done += 300)
    {
        automation[0] = lane.data() + done;
        runner.context.automation = automation;
        (void) runner.run(std::vector<float>(300, 0.0f), 300);
        heapOperations += runner.heapOperations;
    }

    const auto& level = probe->recorded[0];
    REQUIRE(level.size() == lane.size());
    bool exact = true;
    for (std::size_t i = 0; i < lane.size(); ++i)
        exact = exact && level[i] == info.fromNormalised(lane[i]);
    CHECK(exact);
    CHECK(heapOperations == 0);

    // When the automation stops, the value glides from its last value to the user's.
    runner.context.automation = {};
    probe->recorded[0].clear();
    (void) runner.run(std::vector<float>(2000, 0.0f));
    CHECK_THAT(level.front(), WithinAbs(info.fromNormalised(2999.0f / 3000.0f), 0.2));
    CHECK(level[rampLength - 1] == 50.0f);
}

TEST_CASE("An effect that outputs NaN or infinity is reset, silenced and reported",
          "[engine][fx]")
{
    const bool infinity = GENERATE(false, true);
    auto effect = std::make_unique<BrokenEffect>(1000, infinity);
    auto* broken = effect.get();
    Runner runner(std::move(effect));
    const auto resets = broken->resets;

    const std::vector<float> ones(4000, 1.0f);
    const auto output = runner.run(ones, 256);

    CAPTURE(infinity);
    // Blocks of 256: the block holding sample 1000 starts at 768. Everything after
    // that is silence, and no sample is anything but finite.
    bool before = true;
    bool after = true;
    for (std::size_t i = 0; i < output.size(); ++i)
    {
        if (i < 768)
            before = before && output[i] == 1.0f;
        else
            after = after && output[i] == 0.0f;
    }
    CHECK(before);
    CHECK(after);
    CHECK(runner.instance.hasFailed());
    CHECK(broken->resets == resets + 1);

    // Still silent until revived; then the effect plays again (it passes the audio
    // until sample 1000 again), fading in from the dry signal.
    CHECK(runner.run(ones) == std::vector<float>(4000, 0.0f));
    runner.settings.revival = 1;
    const auto revived = runner.run(std::vector<float>(900, 1.0f));
    CHECK_FALSE(runner.instance.hasFailed());
    CHECK(revived == std::vector<float>(900, 1.0f));
    CHECK(runner.heapOperations == 0);
}

TEST_CASE("Blocks longer than the effect's own are processed in pieces", "[engine][fx]")
{
    auto effect = std::make_unique<ParameterProbe>();
    auto* probe = effect.get();
    Runner runner(std::move(effect), false, 64);
    runner.setTargets({0.0f, 0.0f, 0.0f});
    (void) runner.run(std::vector<float>(100, 0.0f), 100);
    probe->recorded[0].clear();

    runner.setTargets({1.0f, 0.0f, 0.0f});
    const auto output = runner.run(std::vector<float>(2000, 0.0f), 1000);

    // The ramp is the same as with small blocks: sample k of the ramp is k / 960.
    const auto& level = probe->recorded[0];
    REQUIRE(level.size() == 2000);
    CHECK_THAT(level[479], WithinAbs(50.0, 1e-4));
    CHECK(level[959] == 100.0f);
    CHECK(output[959] == 100.0f);
    CHECK(runner.heapOperations == 0);
}

TEST_CASE("allFinite finds a single NaN or infinity", "[engine][fx]")
{
    std::vector<float> left(1000, 0.25f);
    std::vector<float> right(1000, -0.25f);
    const float* const channels[] = {left.data(), right.data()};
    CHECK(daw::engine::fx::allFinite(channels, 2, 1000));

    right[777] = std::numeric_limits<float>::infinity();
    CHECK_FALSE(daw::engine::fx::allFinite(channels, 2, 1000));
    right[777] = std::numeric_limits<float>::quiet_NaN();
    CHECK_FALSE(daw::engine::fx::allFinite(channels, 2, 1000));
    CHECK(daw::engine::fx::allFinite(channels, 2, 777));
}
