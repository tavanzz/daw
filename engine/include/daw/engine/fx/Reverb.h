#pragma once

#include "daw/engine/fx/Effect.h"

#include <array>
#include <vector>

namespace daw::engine::fx
{

/**
    An algorithmic reverb: a feedback delay network of 16 lines.

    - The input (in mono) goes through the pre-delay and four allpass diffusers into
      the network. Each line's output is low-passed (damping), mixed with the others
      by an orthogonal (Hadamard) matrix, and fed back with a gain set so that the
      line loses 60 dB in the decay time: g = 10^(-3 d / (T60 rate)) for a line of
      d samples. The matrix keeps energy and the filters never amplify, so the
      network is stable and decays in the decay time at low frequencies, faster
      above them (damping).
    - Half the lines are read with a slight, slow modulation of their length, which
      smooths the metallic ringing of long decays. Fractional lengths are read
      through allpass interpolation, which neither amplifies nor loses anything.
    - Size scales the line lengths; when it changes the lengths glide (an eighth of a
      sample per sample).
    - Two orthogonal sums of the lines give left and right, then width, the low and
      high cut of the reverberated sound (off at the ends of their ranges) and the
      mix with the dry signal.
*/
class Reverb final : public Effect
{
public:
    static constexpr const char* typeId = "reverb";
    static constexpr int numLines = 16;
    static constexpr double maximumPreDelaySeconds = 0.25;

    enum Parameter
    {
        preDelay,
        decay,
        size,
        damping,
        lowCut,
        highCut,
        width,
        mix
    };

    Reverb();

    [[nodiscard]] const EffectDescription& getDescription() const noexcept override;
    void prepare(const ProcessSetup& setup) override;
    void reset() noexcept override;
    void process(const ProcessBlock& block) noexcept override;
    [[nodiscard]] double getTailSeconds(std::span<const float> values) const override;

private:
    struct Line
    {
        std::vector<float> buffer;
        int position = 0;
        double length = 0.0; // samples, gliding to target
        double target = 0.0;
        double gain = 0.0;
        double damped = 0.0;
        double phase = 0.0; // of its modulation
        double rate = 0.0;  // radians per sample, 0 for an unmodulated line
        double allpass = 0.0; // the interpolator's last output

        /** The line delayed by a fractional number of samples, through a first-order
            allpass interpolator: no loss at any frequency, so the decay time
            holds. */
        [[nodiscard]] double read(double delay) noexcept;
    };

    struct Allpass
    {
        std::vector<float> buffer;
        int position = 0;
        [[nodiscard]] double process(double x) noexcept;
    };

    void setSize(float percent, bool glide) noexcept;
    void setGains(float decaySeconds) noexcept;

    EffectDescription description;
    double sampleRate = 48000.0;
    std::array<Line, numLines> lines;
    std::array<Allpass, 4> diffusers;
    std::vector<float> preDelayLine;
    int preDelayPosition = 0;

    float sizeFor = -1.0f;
    float decayFor = -1.0f;
    float lowCutFor = -1.0f;
    float highCutFor = -1.0f;
    double lowCutCoefficient = 0.0;
    double highCutCoefficient = 0.0;
    double lowCutState[2] = {0.0, 0.0};
    double highCutState[2] = {0.0, 0.0};

    int memory = 1;    // the longest way a sample can stay in the reverb
    int quietFor = 0;  // samples in a row with nothing written anywhere
    bool started = false;
};

} // namespace daw::engine::fx
