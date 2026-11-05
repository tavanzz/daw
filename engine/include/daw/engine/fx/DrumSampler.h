#pragma once

#include "daw/engine/fx/SampleInstrument.h"

namespace daw::engine::fx
{

/**
    Sixteen pads, a sample each (the slots "pad1" to "pad16"), each on a key (by
    default 36 to 51, General MIDI's drums), with a level, a pan, a tuning, a decay
    and a choke group (a hit fades out the others of its group: the closed and pedal
    hi-hats choke the open one; by default pads 7, 9 and 11, keys 42, 44 and 46, are
    group 1). Hits play to the end of their sample or of their decay, whatever the
    note-off (one-shot); hitting a pad again fades its last hit out (see
    SampleInstrument).
*/
class DrumSampler final : public SampleInstrument
{
public:
    static constexpr const char* typeId = "drums";
    static constexpr int numPads = 16;
    static constexpr int firstKey = 36;
    static constexpr int numChokeGroups = 4;

    /** Per pad, in this order, then the kit's. */
    enum PadParameter
    {
        padKey,
        padLevel,
        padPan,
        padTune,
        padDecay,
        padChoke,
        parametersPerPad
    };
    enum KitParameter
    {
        velocity = numPads * parametersPerPad,
        polyphony,
        level,
        numParameters
    };

    /** The slot key of pad index (0 to 15): "pad1" to "pad16". */
    [[nodiscard]] static juce::String slotOf(int pad);

    /** The parameter index of one pad's parameter. */
    [[nodiscard]] static int parameterOf(int pad, PadParameter parameter) noexcept
    {
        return pad * parametersPerPad + parameter;
    }

    DrumSampler();

    [[nodiscard]] const EffectDescription& getDescription() const noexcept override;
    [[nodiscard]] double getTailSeconds(std::span<const float> values) const override;

private:
    void readParameters(const ProcessBlock& block, int sample) noexcept override;
    [[nodiscard]] bool zoneFor(int key, Zone& zone) const noexcept override;
    [[nodiscard]] int polyphonyNow() const noexcept override;
    [[nodiscard]] double bendRangeNow() const noexcept override { return 0.0; }
    [[nodiscard]] float levelNow() const noexcept override;

    EffectDescription description;
    std::array<float, numParameters> values{};
};

} // namespace daw::engine::fx
