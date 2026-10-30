#pragma once

#include "daw/engine/Arrangement.h"

#include <juce_core/juce_core.h>

#include <cstdint>
#include <functional>

namespace daw::engine::io
{

/** How to write an export. The sample rate is the arrangement's. */
struct BounceSettings
{
    juce::File file;              ///< replaced only once the export is complete
    int bitsPerSample = 24;       ///< 16 or 24 (integer), 32 (floating point)
    bool dither = false;          ///< TPDF dither of ±1 LSB before quantising to 16 or 24 bits
    juce::int64 start = 0;        ///< timeline range [start, end), in samples
    juce::int64 end = 0;
    std::uint32_t ditherSeed = 1; ///< the same seed gives the same file
};

struct BounceResult
{
    juce::Result result = juce::Result::ok();
    bool cancelled = false;
    float peak = 0.0f; ///< highest absolute sample of the mix, before quantising (1 = 0 dBFS)
    juce::int64 samplesWritten = 0;
};

/** Integer quantisation used by exports: round(sample × 2^(bits−1)), clipped to the
    integer range, so +1.0 becomes the largest positive value and −1.0 the smallest.
    ditherLsb is added before rounding, in units of the last bit. */
[[nodiscard]] int quantise(float sample, int bitsPerSample, float ditherLsb = 0.0f) noexcept;

/**
    Mixes [settings.start, settings.end) of the arrangement with OfflineRenderer and
    writes it to a stereo WAV file. The file is written next to its target first and
    replaces it only when complete, so a failed or cancelled export never leaves a
    damaged file behind.

    keepGoing is called after each block with the progress (0 to 1); returning false
    cancels. Any thread except the audio thread.
*/
[[nodiscard]] BounceResult bounceToWav(const Arrangement& arrangement,
                                       const BounceSettings& settings,
                                       const std::function<bool(double)>& keepGoing = {});

} // namespace daw::engine::io
