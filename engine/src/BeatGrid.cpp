#include "daw/engine/BeatGrid.h"

#include <cmath>

namespace daw::engine
{

juce::int64 BeatGrid::firstBeatFrom(juce::int64 position) const noexcept
{
    // The estimate can be off where rounding moved a beat; the comparisons settle it
    // against the real positions.
    auto beat = static_cast<juce::int64>(
        std::floor(static_cast<double>(position) / approximateBeatLength()));

    while (beatPosition(beat) < position)
        ++beat;
    while (beatPosition(beat - 1) >= position)
        --beat;

    return beat;
}

bool BeatGrid::isDownbeat(juce::int64 beat) const noexcept
{
    const auto perBar = static_cast<juce::int64>(beatsPerBar());
    return ((beat % perBar) + perBar) % perBar == 0;
}

} // namespace daw::engine
