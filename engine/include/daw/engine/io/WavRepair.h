#pragma once

#include <juce_core/juce_core.h>

#include <limits>

namespace daw::engine::io
{

/** Outcome of repairWav(). */
struct WavRepairResult
{
    juce::Result result = juce::Result::ok();
    bool changed = false;       ///< the header (or a partial last frame) was fixed
    juce::int64 numFrames = 0;  ///< samples per channel the file now holds
    double sampleRate = 0.0;
    int numChannels = 0;
};

/**
    Makes a WAV file whose writing was cut short (a crash, a power cut) readable
    again: the lengths in its header are recomputed from the size of the file, a
    partial last frame is cut off, and a file too long for a RIFF header becomes an
    RF64 one (using the space JUCE's writer reserves for that). The audio itself is
    never touched.

    Works on RIFF and RF64 files with a "fmt " chunk before the "data" chunk, which
    is how RecordingWriter writes them. Any thread except the audio thread.
    rf64Threshold is for tests: data at least that long needs RF64.
*/
WavRepairResult repairWav(const juce::File& file,
                          juce::int64 rf64Threshold = std::numeric_limits<juce::uint32>::max());

} // namespace daw::engine::io
