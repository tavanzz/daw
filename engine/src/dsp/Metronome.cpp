#include "daw/engine/dsp/Metronome.h"

#include <algorithm>
#include <cmath>
#include <numbers>

namespace daw::engine::dsp
{

namespace
{

// A decaying tone that starts at its peak, so that its first sample marks the beat,
// and ends on a short linear fade to exactly 0.
std::vector<float> makeClick(double sampleRate, double frequency, double amplitude)
{
    const auto length = static_cast<std::size_t>(std::lround(Metronome::clickSeconds * sampleRate));
    const double decaySamples = 0.006 * sampleRate;
    const double fadeSamples = 0.002 * sampleRate;

    std::vector<float> click(length);
    for (std::size_t i = 0; i < length; ++i)
    {
        const auto n = static_cast<double>(i);
        const double tail = std::min(1.0, static_cast<double>(length - 1 - i) / fadeSamples);
        click[i] = static_cast<float>(amplitude *
                                      std::cos(2.0 * std::numbers::pi * frequency * n / sampleRate) *
                                      std::exp(-n / decaySamples) * tail);
    }

    return click;
}

} // namespace

void Metronome::prepare(double sampleRate)
{
    jassert(sampleRate > 0.0);
    stop();
    accent = makeClick(sampleRate, 1600.0, 1.0);
    normal = makeClick(sampleRate, 1000.0, 0.6);
}

void Metronome::render(const BeatGrid* grid, juce::int64 timelineStart, float gain, float* left,
                       float* right, int numSamples) noexcept
{
    int done = 0;

    if (grid != nullptr)
    {
        const juce::int64 end = timelineStart + numSamples;
        for (auto beat = grid->firstBeatFrom(timelineStart);; ++beat)
        {
            const juce::int64 position = grid->beatPosition(beat);
            if (position >= end)
                break;

            const auto offset = static_cast<int>(position - timelineStart);
            ring(left, right, done, offset);

            ringing = &getClick(grid->isDownbeat(beat));
            ringingPosition = 0;
            ringingGain = gain;
            done = offset;
        }
    }

    ring(left, right, done, numSamples);
}

void Metronome::ring(float* left, float* right, int from, int to) noexcept
{
    if (ringing == nullptr || to <= from)
        return;

    const auto count = std::min(static_cast<std::size_t>(to - from), ringing->size() - ringingPosition);
    const float* click = ringing->data() + ringingPosition;

    for (std::size_t i = 0; i < count; ++i)
    {
        const float sample = click[i] * ringingGain;
        left[static_cast<std::size_t>(from) + i] += sample;
        right[static_cast<std::size_t>(from) + i] += sample;
    }

    ringingPosition += count;
    if (ringingPosition >= ringing->size())
        ringing = nullptr;
}

} // namespace daw::engine::dsp
