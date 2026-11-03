#pragma once

#include "daw/engine/fx/Effect.h"
#include "daw/engine/fx/GainReductionMeter.h"

#include <atomic>

namespace daw::engine::fx
{

/**
    A feed-forward compressor, after Giannoulis, Massberg and Reiss, "Digital
    Dynamic Range Compressor Design - A Tutorial and Analysis" (JAES, 2012).

    - The level is the larger absolute sample of the two channels (linked), in dB.
    - The static curve (staticCurve()): unchanged below the knee, threshold + (level
      - threshold) / ratio above it, a quadratic bend of the knee's width between.
    - The gain reduction it asks for is smoothed in dB by a peak detector with
      separate attack and release: they are time constants, so after a step the
      reduction covers 1 - 1/e (63 %) of the way in that time.
    - Then the makeup gain, and the mix with the dry signal (parallel compression).

    No lookahead: no latency. getGainReduction() is for the UI's meter, and
    getInputLevel() the level the curve is at.
*/
class Compressor final : public Effect
{
public:
    static constexpr const char* typeId = "compressor";

    enum Parameter
    {
        threshold,
        ratio,
        knee,
        attack,
        release,
        makeup,
        mix
    };

    Compressor();

    [[nodiscard]] const EffectDescription& getDescription() const noexcept override;
    void prepare(const ProcessSetup& setup) override;
    void reset() noexcept override;
    void process(const ProcessBlock& block) noexcept override;
    [[nodiscard]] double getTailSeconds(std::span<const float> values) const override;

    /** The output level (dB) of a steady input level (dB). */
    [[nodiscard]] static double staticCurve(double inputDb, double thresholdDb, double ratio,
                                            double kneeDb) noexcept;

    [[nodiscard]] GainReductionMeter& getGainReduction() noexcept { return reduction; }

    /** The UI's reader: the largest input level (dB) since its last call. */
    [[nodiscard]] float takeInputLevel() noexcept
    {
        return inputLevel.exchange(-200.0f, std::memory_order_relaxed);
    }

private:
    EffectDescription description;
    double sampleRate = 48000.0;
    double smoothed = 0.0; // the gain reduction, dB
    float attackFor = -1.0f;
    float releaseFor = -1.0f;
    double attackCoefficient = 0.0;
    double releaseCoefficient = 0.0;
    GainReductionMeter reduction;
    std::atomic<float> inputLevel{-200.0f};
};

} // namespace daw::engine::fx
