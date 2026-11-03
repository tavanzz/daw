#pragma once

#include <vector>

namespace daw::engine::dsp
{

/**
    Delays audio by a whole number of samples, exactly: what comes out is what went
    in delay samples earlier, bit for bit (silence before the first input). Used to
    keep paths of different latency aligned.

    prepare() allocates; everything else is real-time safe. One thread at a time.
*/
class DelayLine
{
public:
    /** Room for numChannels channels and delays up to maximumDelay; clears. */
    void prepare(int numChannels, int maximumDelay);

    [[nodiscard]] int getMaximumDelay() const noexcept { return maximumDelay; }
    [[nodiscard]] int getDelay() const noexcept { return delay; }

    /** Clamped to 0..getMaximumDelay(). What is stored is kept: the output jumps to
        the audio of the new delay. */
    void setDelay(int samples) noexcept;

    /** Silence, as if just prepared. */
    void clear() noexcept;

    /** Takes over what previous holds (they swap their memory), keeping this line's
        delay, if previous has the same channels and room for that delay; otherwise
        does nothing. Real-time safe: the same line, carried from one mix state to
        the next, goes on without a gap. */
    void adopt(DelayLine& previous) noexcept;

    /** Delays numSamples samples of each channel in place. */
    void process(float* const* channels, int numChannels, int numSamples) noexcept;

    /** The same, reading input and writing the delayed audio elsewhere. */
    void process(const float* const* input, float* const* output, int numChannels,
                 int numSamples) noexcept;

private:
    std::vector<std::vector<float>> buffers; // one per channel, a power of two long
    int mask = 0;
    int writePosition = 0;
    int delay = 0;
    int maximumDelay = 0;
};

} // namespace daw::engine::dsp
