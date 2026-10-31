#pragma once

#include "daw/engine/BeatGrid.h"
#include "daw/model/TempoMap.h"

namespace daw::controller
{

/**
    The engine's beats from the project's tempo map: beat b of the timeline is beat
    b mod n of bar floor(b / n) (n beats per bar), at TempoMap::beatStart() converted
    to the nearest sample, exactly as the rulers and the grid place it.

    Plain arithmetic on a copy of the tempo map: real-time safe once built.
*/
class TempoBeatGrid final : public engine::BeatGrid
{
public:
    TempoBeatGrid(const model::TempoMap& tempoMap, double sampleRate);

    [[nodiscard]] juce::int64 beatPosition(juce::int64 beat) const noexcept override;
    [[nodiscard]] int beatsPerBar() const noexcept override;
    [[nodiscard]] double approximateBeatLength() const noexcept override;

private:
    model::TempoMap tempo;
    double rate;
};

/** Where a count-in of the given number of bars starts, for playback from
    position: that many bars earlier (possibly before the start of the timeline). */
[[nodiscard]] model::Ticks countInStart(const model::TempoMap& tempoMap, model::Ticks position,
                                        int bars);

} // namespace daw::controller
