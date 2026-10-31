#pragma once

namespace daw::engine::dsp
{

/**
    A gain that moves in a straight line to a new value over a fixed number of
    samples, so that volume, pan and mute changes never step (no zipper noise).

    Every gain is computed from where the ramp started, never accumulated, so the
    same samples get the same gains however a render is cut into blocks: real-time
    playback and an offline export agree to the bit. The last sample of a ramp gets
    exactly the target.

    Real-time safe; one thread at a time.
*/
class GainRamp
{
public:
    /** At 1, not ramping. */
    GainRamp() = default;

    /** At initial, not ramping. */
    explicit GainRamp(float initial) noexcept
        : start(initial)
        , end(initial)
    {
    }
    /** Jumps to value: no ramp. */
    void reset(float value) noexcept;

    /** Ramps from the current gain to target over rampLength samples (at once if
        rampLength < 1). Setting the target it already has does nothing. */
    void setTarget(float target, int rampLength) noexcept;

    [[nodiscard]] float getTarget() const noexcept { return end; }
    [[nodiscard]] bool isRamping() const noexcept { return position < length; }

    /** The gain of the last sample processed (the target once the ramp is over). */
    [[nodiscard]] float getCurrent() const noexcept;

    /** Multiplies numSamples samples of each channel by the gain, and moves on. */
    void apply(float* const* channels, int numChannels, int numSamples) noexcept;

    /** Moves on numSamples without processing anything. */
    void skip(int numSamples) noexcept;

private:
    [[nodiscard]] float gainAt(int step) const noexcept;

    float start = 1.0f;
    float end = 1.0f;
    int length = 0;
    int position = 0;
};

} // namespace daw::engine::dsp
