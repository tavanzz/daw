#pragma once

#include "daw/engine/fx/Effect.h"

#include <array>
#include <span>

namespace daw::engine::fx
{

/**
    A parametric equaliser of eight bands, in this order: a high-pass, a low shelf,
    four bells, a high shelf and a low-pass, plus an output gain.

    - Bells and shelves have a frequency, a gain (+-24 dB) and a Q; they are the
      Audio EQ Cookbook's (RBJ) peaking and shelving filters. A band at 0 dB passes
      the audio unchanged, bit for bit.
    - The high- and low-pass are Butterworth filters of 6 to 48 dB per octave: -3 dB
      at their frequency.
    - Every filter is a trapezoidal state-variable filter (or one-pole, for odd
      orders): the bilinear transform with the frequency prewarped, so the response
      at the band's frequency is exactly the analog prototype's, and the filters stay
      stable and quiet while their parameters move from sample to sample.
    - Switching a band on or off, or changing a slope, crossfades over 10 ms.

    magnitude() gives the exact response of a set of parameter values, for drawing
    the curve: the same formula the filters realise.
*/
class Equaliser final : public Effect
{
public:
    static constexpr const char* typeId = "equaliser";

    enum class BandType
    {
        highPass,
        lowShelf,
        bell,
        highShelf,
        lowPass
    };

    static constexpr int numBands = 8;
    static constexpr int numBells = 4;

    /** Slopes of the high- and low-pass, in dB per octave (choice values 0 to 5). */
    static constexpr std::array<int, 6> slopes{6, 12, 18, 24, 36, 48};

    [[nodiscard]] static BandType typeOf(int band) noexcept;

    /** Where each band's parameters are: its switch, frequency, gain (shelves and
        bells only; -1 otherwise), Q (shelves and bells) and slope (passes). */
    struct BandParameters
    {
        int on = -1;
        int frequency = -1;
        int gain = -1;
        int q = -1;
        int slope = -1;
    };
    [[nodiscard]] static BandParameters parametersOf(int band) noexcept;
    [[nodiscard]] static int outputGainParameter() noexcept;

    Equaliser();

    [[nodiscard]] const EffectDescription& getDescription() const noexcept override;
    void prepare(const ProcessSetup& setup) override;
    void reset() noexcept override;
    void process(const ProcessBlock& block) noexcept override;

    /** The gain (linear) at frequency of an equaliser with these parameter values
        (in their units, one per parameter) at sampleRate. */
    [[nodiscard]] static double magnitude(std::span<const float> values, double frequency,
                                          double sampleRate);

    /** The same for one band, whether or not it is switched on. */
    [[nodiscard]] static double bandMagnitude(std::span<const float> values, int band,
                                              double frequency, double sampleRate);

    /** One trapezoidal state-variable filter section. */
    struct Section
    {
        double a1 = 0.0, a2 = 0.0, a3 = 0.0;
        double m0 = 1.0, m1 = 0.0, m2 = 0.0;
        double s1[2] = {0.0, 0.0};
        double s2[2] = {0.0, 0.0};
        bool firstOrder = false; // a one-pole: a1 = G, m0/m2 pick high or low

        [[nodiscard]] double process(int channel, double x) noexcept;
        void clear() noexcept;
        [[nodiscard]] bool isClear() const noexcept;
    };

    /** A band as it is processed: up to four sections in series. */
    struct BandFilter
    {
        static constexpr int maximumSections = 5;
        std::array<Section, maximumSections> sections;
        int numSections = 0;

        void design(BandType type, double frequency, double gainDb, double q, int slopeChoice,
                    double sampleRate) noexcept;
        [[nodiscard]] double process(int channel, double x) noexcept;
        void clear() noexcept;
        [[nodiscard]] bool isClear() const noexcept;
    };

    /** Nothing left inside and silence coming in: it skips its work. For tests. */
    [[nodiscard]] bool isResting() const noexcept { return silent; }

private:
    struct Band
    {
        BandFilter filter;
        BandFilter previous; // the old setting, while a switch crossfades
        bool on = false;
        int slope = 0;
        int crossfadeLeft = 0; // samples of crossfade still to do
        bool previousOn = false;
        float designedFrequency = -1.0f;
        float designedGain = 0.0f;
        float designedQ = 0.0f;
    };

    void designBand(int band, const ProcessBlock& block, int sample) noexcept;

    EffectDescription description;
    double sampleRate = 48000.0;
    int crossfadeLength = 480;
    std::array<Band, numBands> bands;
    bool silent = true; // input and every filter state exactly zero
    bool fresh = true;  // nothing processed since prepare() or reset()
};

} // namespace daw::engine::fx
