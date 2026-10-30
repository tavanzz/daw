#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

namespace daw::engine
{

/** Decoded audio at the sample rate it will be played at.

    Created off the audio thread (see loadAudioFile()) and never modified after it
    has been handed to the engine. */
struct AudioData
{
    juce::AudioBuffer<float> samples;
    double sampleRate = 0.0;
};

} // namespace daw::engine
