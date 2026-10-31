#include "daw/controller/TempoBeatGrid.h"

namespace daw::controller
{

TempoBeatGrid::TempoBeatGrid(const model::TempoMap& tempoMap, double sampleRate)
    : tempo(tempoMap)
    , rate(sampleRate)
{
    jassert(sampleRate > 0.0);
}

juce::int64 TempoBeatGrid::beatPosition(juce::int64 beat) const noexcept
{
    const auto perBar = static_cast<juce::int64>(beatsPerBar());
    const auto bar = (beat >= 0 ? beat : beat - (perBar - 1)) / perBar; // rounded down
    const auto beatInBar = static_cast<int>(beat - bar * perBar);
    return model::ticksToSamples(tempo.beatStart(bar, beatInBar), rate);
}

int TempoBeatGrid::beatsPerBar() const noexcept
{
    return tempo.getTimeSignature().numerator;
}

double TempoBeatGrid::approximateBeatLength() const noexcept
{
    return tempo.ticksPerBeat() * rate / static_cast<double>(model::ticksPerSecond);
}

model::Ticks countInStart(const model::TempoMap& tempoMap, model::Ticks position, int bars)
{
    return position - tempoMap.barStart(bars);
}

} // namespace daw::controller
