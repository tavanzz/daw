#pragma once

#include "daw/engine/fx/Effect.h"
#include "daw/engine/fx/GainReductionMeter.h"

#include <vector>

namespace daw::engine::fx
{

/**
    A lookahead brickwall limiter: the output never goes above the ceiling.

    The audio is delayed by the lookahead (lookaheadSeconds; its latency). For each
    sample the gain that keeps it under the ceiling is worked out; the gain applied
    is the lowest of those over the lookahead, recovering at the release time
    (instantly down, exponentially up), then averaged over the lookahead so that it
    bends smoothly. Every gain averaged covers the delayed sample's own, so the
    output stays under the ceiling by construction; a final clip only catches
    rounding. The two channels share one gain. Sample peaks (not inter-sample).
*/
class Limiter final : public Effect
{
public:
    static constexpr const char* typeId = "limiter";
    static constexpr double lookaheadSeconds = 0.0015;

    enum Parameter
    {
        inputGain,
        ceiling,
        release
    };

    Limiter();

    [[nodiscard]] const EffectDescription& getDescription() const noexcept override;
    void prepare(const ProcessSetup& setup) override;
    void reset() noexcept override;
    void process(const ProcessBlock& block) noexcept override;
    [[nodiscard]] int getLatency() const noexcept override { return lookahead; }

    [[nodiscard]] GainReductionMeter& getGainReduction() noexcept { return reduction; }

    /** For tests: how far above the ceiling (relative) a sample was before the final
        clip; only rounding should ever make this more than 0. */
    [[nodiscard]] double getLargestExcess() const noexcept { return largestExcess; }

private:
    EffectDescription description;
    double sampleRate = 48000.0;
    int lookahead = 72;

    // Delayed audio, lookahead samples per channel.
    std::vector<float> delayed[2];
    int delayPosition = 0;

    // The lowest required gain over the last lookahead + 1 samples: a monotonic
    // queue of (sample index, gain), oldest first.
    std::vector<double> queueGains;
    std::vector<juce::int64> queueIndices;
    int queueHead = 0;
    int queueSize = 0;
    juce::int64 sampleIndex = 0;

    // The gains being averaged, and their sum.
    std::vector<double> averaged;
    int averagePosition = 0;
    double sum = 0.0;
    double released = 1.0;
    float releaseFor = -1.0f;
    double releaseCoefficient = 0.0;

    GainReductionMeter reduction;
    double largestExcess = 0.0;
};

} // namespace daw::engine::fx
