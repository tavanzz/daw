#pragma once

#include "daw/engine/fx/Effect.h"

#include <array>
#include <vector>

namespace daw::engine::fx
{

/**
    A stereo echo, in milliseconds or synced to the tempo.

    - The delay is a whole number of samples: an echo comes exactly that many
      samples later (synced: the note's length at the project's tempo, rounded to
      the sample). At most maximumSeconds.
    - Feedback is at most 95 %, and the filters in the loop never amplify, so echoes
      always die away.
    - Low cut and high cut (one-pole filters) shape the echoes; at the ends of their
      ranges they are off, and the echoes are exact copies.
    - Ping-pong: the input (in mono) goes left, and each echo crosses to the other
      side.
    - When the delay time changes, the output crossfades from the old tap to the
      new over 50 ms (no tape-like pitch bend), one change at a time.
*/
class Delay final : public Effect
{
public:
    static constexpr const char* typeId = "delay";
    static constexpr double maximumSeconds = 4.0;
    static constexpr double timeChangeSeconds = 0.05;

    enum Parameter
    {
        sync,
        time,
        note,
        feedback,
        mix,
        pingPong,
        lowCut,
        highCut
    };

    /** Note lengths for sync, shortest first, in quarter notes, with their names. */
    struct NoteValue
    {
        const char* name;
        double quarters;
    };
    static const std::array<NoteValue, 18> noteValues;

    Delay();

    [[nodiscard]] const EffectDescription& getDescription() const noexcept override;
    void prepare(const ProcessSetup& setup) override;
    void reset() noexcept override;
    void process(const ProcessBlock& block) noexcept override;
    [[nodiscard]] double getTailSeconds(std::span<const float> values) const override;

    /** The delay in samples for these settings. */
    [[nodiscard]] static int delaySamples(bool synced, double milliseconds, int noteIndex,
                                          double tempo, double sampleRate) noexcept;

private:
    struct OnePole
    {
        double state = 0.0;
        [[nodiscard]] double lowPass(double x, double g) noexcept
        {
            const double v = (x - state) * g;
            const double low = v + state;
            state = low + v;
            return low;
        }
    };

    [[nodiscard]] float tap(int channel, int delay) const noexcept;

    EffectDescription description;
    double sampleRate = 48000.0;
    int capacity = 1;
    std::array<std::vector<float>, 2> lines;
    int writePosition = 0;

    int delay = 1;          // the tap being heard
    int previousDelay = 1;  // the one it is crossfading from
    int fadeLength = 2400;
    int fadeDone = 0;       // fadeLength when no crossfade runs
    bool started = false;

    std::array<OnePole, 2> lowPassFilters;
    std::array<OnePole, 2> highPassFilters; // the low-pass of a high-pass
    float lowCutFor = -1.0f;
    float highCutFor = -1.0f;
    double lowCutCoefficient = 0.0;
    double highCutCoefficient = 0.0;
    int silentFor = 0; // samples written in a row that were exactly zero
};

} // namespace daw::engine::fx
