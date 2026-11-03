#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/Mixer.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <vector>

/**
    A frozen copy of the mixer of version 0.3.0 (Phase 3), the reference for "with
    empty or bypassed effect chains the mix is exactly what Phase 3 made". Do not
    change it: it is what the current mixer is compared with, bit for bit.

    The code below is the v0.3.0 engine/src/Mixer.cpp and dsp/GainRamp.cpp,
    renamed into this namespace; it reads the same Arrangement and MixContext
    (only the fields Phase 3 had).
*/
namespace daw::test::phase3
{

using engine::Arrangement;
using engine::ArrangementTrack;
using engine::ClipSegment;
using engine::InputMonitoring;
using engine::MixContext;

class GainRamp
{
public:
    GainRamp() = default;
    explicit GainRamp(float initial) noexcept
        : start(initial)
        , end(initial)
    {
    }
    void reset(float value) noexcept;
    void setTarget(float target, int rampLength) noexcept;
    [[nodiscard]] float getTarget() const noexcept { return end; }
    [[nodiscard]] bool isRamping() const noexcept { return position < length; }
    [[nodiscard]] float getCurrent() const noexcept;
    void apply(float* const* channels, int numChannels, int numSamples) noexcept;
    void skip(int numSamples) noexcept;

private:
    [[nodiscard]] float gainAt(int step) const noexcept;

    float start = 1.0f;
    float end = 1.0f;
    int length = 0;
    int position = 0;
};

struct MixState
{
    struct TrackGains
    {
        GainRamp left;
        GainRamp right;
        GainRamp timeline;
        GainRamp input{0.0f};
    };

    std::vector<TrackGains> tracks;
    GainRamp master;

    [[nodiscard]] static MixState startingAt(const Arrangement& arrangement);
    void continueFrom(const MixState& previous, const Arrangement& previousArrangement,
                      const Arrangement& arrangement, int rampLength) noexcept;
    [[nodiscard]] bool isHearingInput() const noexcept;
};

class Mixer
{
public:
    static constexpr double gainRampSeconds = 0.02;
    static constexpr double monitorRampSeconds = 0.005;

    void prepare(double sampleRate, int maximumBlockSize);
    [[nodiscard]] int getGainRampLength() const noexcept { return gainRampLength; }
    void render(const Arrangement& arrangement, MixState& state, juce::int64 timelineStart,
                juce::AudioBuffer<float>& output, int outputStart, int numSamples,
                const MixContext& context = {}) noexcept;

private:
    void renderTrack(const ArrangementTrack& track, MixState::TrackGains& gains,
                     juce::int64 timelineStart, int numSamples, const MixContext& context) noexcept;
    void readClips(const ArrangementTrack& track, juce::int64 timelineStart,
                   int numSamples) noexcept;
    void readInput(const ArrangementTrack& track, const MixContext& context,
                   int numSamples) noexcept;

    int gainRampLength = 1;
    int monitorRampLength = 1;
    juce::AudioBuffer<float> scratch;
    juce::AudioBuffer<float> inputScratch;
};

} // namespace daw::test::phase3
