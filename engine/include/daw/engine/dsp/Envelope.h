#pragma once

namespace daw::engine::dsp
{

/**
    An ADSR envelope, sample by sample.

    - Attack: a straight line from the current level to 1, at the slope that goes from
      0 to 1 in exactly the attack time.
    - Decay: exponential towards the sustain level, the distance shrinking by 60 dB in
      the decay time; then it holds the sustain level.
    - Release: exponential towards 0, 60 dB in the release time; the envelope ends
      (isActive() false) below -100 dB, or once the decay reaches a sustain of 0.

    Times can change at any moment (automation); the next sample follows them.
    Real-time safe.
*/
class Envelope
{
public:
    static constexpr double endLevel = 1.0e-5; ///< -100 dB

    /** Seconds, and the sustain level 0 to 1. */
    void setTimes(double attackSeconds, double decaySeconds, double sustainLevel,
                  double releaseSeconds, double sampleRate) noexcept;

    void noteOn() noexcept { stage = Stage::attack; }
    void noteOff() noexcept;
    void reset() noexcept;

    /** The next sample's level. */
    double next() noexcept;

    /** Advances numSamples samples (for envelopes read at a control rate). */
    void skip(int numSamples) noexcept;

    [[nodiscard]] bool isActive() const noexcept { return stage != Stage::idle; }
    [[nodiscard]] bool isReleasing() const noexcept { return stage == Stage::release; }
    [[nodiscard]] double getLevel() const noexcept { return level; }

private:
    enum class Stage
    {
        idle,
        attack,
        decay,
        sustain,
        release
    };

    Stage stage = Stage::idle;
    double level = 0.0;
    double attackStep = 1.0;
    double decayFactor = 0.0;
    double sustain = 1.0;
    double releaseFactor = 0.0;
};

} // namespace daw::engine::dsp
