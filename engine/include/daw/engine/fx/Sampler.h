#pragma once

#include "daw/engine/fx/SampleInstrument.h"

namespace daw::engine::fx
{

/** The name of a key as the app writes it: middle C (60) is "C3". */
[[nodiscard]] juce::String keyName(int key);

/**
    One sample over the whole keyboard: it plays at its own pitch on the root key and
    is transposed for the others (see SampleInstrument), with a tuning, where it
    starts, a loop (forward with a crossfade, or ping-pong), an ADSR envelope and
    velocity. Its file is the "sample" slot.
*/
class Sampler final : public SampleInstrument
{
public:
    static constexpr const char* typeId = "sampler";
    static constexpr const char* sampleSlot = "sample";

    enum Parameter
    {
        rootKey,
        transpose,
        fine,
        start,
        loop,
        loopStart,
        loopEnd,
        crossfade,
        attack,
        decay,
        sustain,
        release,
        velocity,
        polyphony,
        bendRange,
        level,
        numParameters
    };

    Sampler();

    [[nodiscard]] const EffectDescription& getDescription() const noexcept override;
    [[nodiscard]] double getTailSeconds(std::span<const float> values) const override;

private:
    void readParameters(const ProcessBlock& block, int sample) noexcept override;
    [[nodiscard]] bool zoneFor(int key, Zone& zone) const noexcept override;
    [[nodiscard]] int polyphonyNow() const noexcept override;
    [[nodiscard]] double bendRangeNow() const noexcept override;
    [[nodiscard]] float levelNow() const noexcept override;

    EffectDescription description;
    std::array<float, numParameters> values{};
};

} // namespace daw::engine::fx
