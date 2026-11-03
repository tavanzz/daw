#pragma once

#include "daw/engine/BeatGrid.h"

#include <vector>

namespace daw::engine::dsp
{

/**
    The metronome's clicks, on the exact sample of every beat.

    Two synthesised clicks: a higher, louder one on the first beat of each bar and a
    softer one on the others. A click starts on its beat's sample and rings for
    clickSeconds, across audio blocks; a new click cuts the previous one. Where the
    timeline jumps (a loop, a seek), a ringing click simply finishes.

    prepare() allocates; render() and stop() are real-time safe.
*/
class Metronome
{
public:
    static constexpr double clickSeconds = 0.03;

    /** Message thread, while nothing renders. Builds the clicks for this rate. */
    void prepare(double sampleRate);

    /** Audio thread. Adds to left and right: what is left of the click ringing from
        before, then a click on every beat of grid in [timelineStart, timelineStart +
        numSamples), all scaled by gain. A null grid starts no click. */
    void render(const BeatGrid* grid, juce::int64 timelineStart, float gain, float* left,
                float* right, int numSamples) noexcept;

    /** Audio thread. Silences a ringing click at once. */
    void stop() noexcept { ringing = nullptr; }

    /** The click of a downbeat, or of another beat, at gain 1. */
    [[nodiscard]] const std::vector<float>& getClick(bool downbeat) const noexcept
    {
        return downbeat ? accent : normal;
    }

private:
    void ring(float* left, float* right, int from, int to) noexcept;

    std::vector<float> accent;
    std::vector<float> normal;

    const std::vector<float>* ringing = nullptr;
    std::size_t ringingPosition = 0;
    float ringingGain = 0.0f;
};

} // namespace daw::engine::dsp
