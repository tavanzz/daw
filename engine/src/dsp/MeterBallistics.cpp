#include "daw/engine/dsp/MeterBallistics.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <algorithm>

namespace daw::engine::dsp
{

void MeterBallistics::update(float peak, double elapsedSeconds) noexcept
{
    jassert(elapsedSeconds >= 0.0);

    const float peakDb = juce::Decibels::gainToDecibels(peak, floorDb);
    const auto fall = static_cast<float>(fallRateDbPerSecond * elapsedSeconds);

    // The reading is the maximum over the elapsed time, so the level is the larger
    // of the reading and the previous level after falling for that long.
    levelDb = std::max(peakDb, std::max(floorDb, levelDb - fall));

    if (peakDb >= holdDb)
    {
        holdDb = peakDb;
        timeSinceHold = 0.0;
    }
    else
    {
        const double heldBefore = std::max(0.0, holdSeconds - timeSinceHold);
        timeSinceHold += elapsedSeconds;
        const double fallingTime = std::max(0.0, elapsedSeconds - heldBefore);
        holdDb = std::max(levelDb, holdDb - static_cast<float>(fallRateDbPerSecond * fallingTime));
    }

    if (peak >= 1.0f)
        clipping = true;
}

float MeterBallistics::toProportion(float decibels) noexcept
{
    return std::clamp((decibels - floorDb) / -floorDb, 0.0f, 1.0f);
}

} // namespace daw::engine::dsp
