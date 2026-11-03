#pragma once

#include "daw/engine/dsp/DelayLine.h"
#include "daw/engine/dsp/GainRamp.h"
#include "daw/engine/fx/Effect.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <memory>
#include <span>
#include <vector>

namespace daw::engine::fx
{

/** Memory shared by the effects one thread processes one after the other: the dry
    signal and the per-sample parameter values of the effect being processed. */
class EffectScratch
{
public:
    /** Parameters of one effect that can change from sample to sample in one block;
        any more jump to their value at the block's end. */
    static constexpr int maximumChangingParameters = 64;

    /** Not real-time safe. */
    void prepare(int maximumBlockSize);

    [[nodiscard]] int getMaximumBlockSize() const noexcept { return blockSize; }

private:
    friend class EffectInstance;

    juce::AudioBuffer<float> dry;
    std::vector<float> values; // maximumChangingParameters lanes of blockSize
    std::vector<float> gains;
    int blockSize = 0;
};

/** What the arrangement says about one effect of a chain. */
struct SlotSettings
{
    /** Normalised values the user set, one per parameter (missing ones take their
        defaults). They are reached through a ramp. */
    std::span<const float> parameters;

    /** False while bypassed, or while being removed: the effect fades out. */
    bool enabled = true;

    /** A new value clears a failure (the user switched the effect back on). */
    std::uint32_t revival = 0;
};

/** What the host knows about one stretch of audio, besides the settings. */
struct SlotContext
{
    TimeInfo time;

    /** Automation: for each parameter, the normalised value of every sample, or null.
        May be shorter than the parameter list, or empty. */
    std::span<const float* const> automation;
};

/**
    One effect as the host runs it: the effect itself plus what the host does around
    it, so that nothing an effect does or a user changes can click.

    - Parameters reach their new values over rampSeconds, in the normalised domain;
      choices jump. Automated parameters take the automation's values exactly; when
      automation starts it is reached over rampSeconds, when it stops the ramp goes
      on from its last value.
    - Adding, removing and bypassing crossfade between the effect's output and the
      dry signal over fadeSeconds. A bypassed effect is not processed; switched back
      on, it starts from a clean state (no stale echoes).
    - The dry signal is delayed by the effect's latency, so bypassing never moves
      the audio in time.
    - An effect that outputs a NaN or an infinity is reset and silenced (its output
      becomes silence: passing the dry signal could be much louder), and reported
      by hasFailed(), until SlotSettings::revival changes.

    Created and destroyed on the message thread (or an export's); process() on the
    audio thread. Persistent across arrangements: every arrangement playing the
    effect refers to the same instance, so its state goes on when anything changes.
*/
class EffectInstance
{
public:
    static constexpr double fadeSeconds = 0.01;
    static constexpr double rampSeconds = 0.02;
    static constexpr int numChannels = 2;

    /** Prepares the effect for setup (stereo). fadeIn: it starts dry and fades in on
        its first block (an effect added while audio may be playing); otherwise it is
        in, at its parameter values, from the first sample. */
    EffectInstance(std::unique_ptr<Effect> effectToHost, const ProcessSetup& setup,
                   bool fadeIn = false);

    EffectInstance(const EffectInstance&) = delete;
    EffectInstance& operator=(const EffectInstance&) = delete;

    [[nodiscard]] Effect& getEffect() noexcept { return *effect; }
    [[nodiscard]] const Effect& getEffect() const noexcept { return *effect; }
    [[nodiscard]] const EffectDescription& getDescription() const noexcept
    {
        return effect->getDescription();
    }
    [[nodiscard]] const ProcessSetup& getSetup() const noexcept { return setup; }
    [[nodiscard]] int getLatency() const noexcept { return latency; }

    /** Audio thread. Processes numSamples samples of both channels in place (in
        pieces no longer than the prepared block size or scratch's). */
    void process(float* const* channels, int numSamples, const SlotSettings& settings,
                 const SlotContext& context, EffectScratch& scratch) noexcept;

    /** Any thread. Whether the effect was silenced for a NaN or an infinity. */
    [[nodiscard]] bool hasFailed() const noexcept { return failed.load(std::memory_order_relaxed); }

    /** The thread that processes: whether the effect runs (it is not fully dry). */
    [[nodiscard]] bool isRunning() const noexcept { return started && !sleeping; }

    /** The thread that processes: each parameter's value at the last sample
        processed, in its units. */
    [[nodiscard]] std::span<const float> getCurrentValues() const noexcept { return lastValues; }

private:
    void processPiece(float* const* channels, int numSamples, const SlotSettings& settings,
                      const SlotContext& context, int offset, EffectScratch& scratch) noexcept;
    void start(const SlotSettings& settings, const SlotContext& context) noexcept;
    void updateParameters(const SlotSettings& settings, const SlotContext& context, int offset,
                          int numSamples, bool needValues, EffectScratch& scratch) noexcept;
    [[nodiscard]] float targetOf(const SlotSettings& settings, std::size_t index) const noexcept;
    void fail(float* const* channels, int numSamples) noexcept;

    std::unique_ptr<Effect> effect;
    ProcessSetup setup;
    int latency = 0;
    int fadeLength = 1;
    int rampLength = 1;
    bool fadesIn = false;

    std::vector<ParameterMapping> mappings;
    std::vector<float> defaults;        // normalised
    std::vector<dsp::GainRamp> ramps;   // normalised values
    std::vector<dsp::GainRamp> blends;  // from the ramp to automation, as it starts
    std::vector<float> lastValues;      // in units
    std::vector<float> lastNormalised;  // what lastValues was computed from
    std::vector<const float*> changing; // per-sample values for this piece, or null

    dsp::GainRamp wet;
    dsp::DelayLine dryDelay;
    bool started = false;
    bool sleeping = false;
    bool failedHere = false;
    std::uint32_t revival = 0;
    std::atomic<bool> failed{false};
};

/** Audio thread. False if any of the samples is a NaN or an infinity. */
[[nodiscard]] bool allFinite(const float* const* channels, int numChannels,
                             int numSamples) noexcept;

} // namespace daw::engine::fx
