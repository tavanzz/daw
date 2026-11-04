#pragma once

#include "daw/engine/dsp/Envelope.h"
#include "daw/engine/dsp/SincInterpolator.h"
#include "daw/engine/fx/Effect.h"

#include <array>
#include <cstdint>

namespace daw::engine::fx
{

/**
    What the sampler and the drum sampler share: voices playing Samples.

    - A note plays a zone: which sample, at which key it sounds at its own pitch, its
      tuning, level and pan, where it starts, how it loops, its envelope, whether it
      plays to its end whatever the note-off (one-shot), its choke group.
    - Pitch: the sample's speed is 2^(semitones / 12), read with an 8-point windowed
      sinc (dsp::SincInterpolator) from the sample's level whose rate keeps that speed
      at most 1 (Sample::levels): played higher, it never aliases.
    - Loops: forward, with a crossfade before the loop's end into what precedes its
      start (so the seam is continuous), or ping-pong. Without a loop a voice ends
      with its sample.
    - Voices are preallocated (32, plus 4 spare ones fading out stolen notes over
      3 ms, as the synth does); a key struck again takes its voice; the sustain pedal
      holds released notes; a choke group's new note fades out the others of its
      group over 5 ms.
    - A sample that changes (another file) silences its voices at once: the old one
      may be gone.

    Parameters are read every controlInterval samples counted from reset(), never per
    host block: the output does not depend on the host's blocks.
*/
class SampleInstrument : public Effect
{
public:
    static constexpr int maximumVoices = 32;
    static constexpr int spareVoices = 4;
    static constexpr int controlInterval = 16;
    static constexpr double stealFadeSeconds = 0.003;
    static constexpr double chokeFadeSeconds = 0.005;

    enum class Loop
    {
        off,
        forward,
        pingPong
    };

    struct Zone
    {
        int slot = 0;          ///< which of ProcessBlock::samples
        double rootKey = 60.0; ///< the key at which it plays at its own pitch
        double tune = 0.0;     ///< semitones
        float gain = 1.0f;
        float pan = 0.0f;      ///< -1 to 1
        double start = 0.0;    ///< fraction of its length
        Loop loop = Loop::off;
        double loopStart = 0.0; ///< fractions of its length
        double loopEnd = 1.0;
        double crossfadeSeconds = 0.0;
        bool oneShot = false;
        int chokeGroup = 0; ///< 0: none
        double attack = 0.001; ///< seconds
        double decay = 0.3;
        double sustain = 1.0;
        double release = 0.2;
        float velocitySensitivity = 1.0f;
    };

    void prepare(const ProcessSetup& setup) override;
    void reset() noexcept override;
    void process(const ProcessBlock& block) noexcept override;

    /** The thread that processes: voices sounding now. */
    [[nodiscard]] int getActiveVoices() const noexcept;

protected:
    /** At every control tick: the instrument's parameters at that sample. */
    virtual void readParameters(const ProcessBlock& block, int sample) noexcept = 0;

    /** The zone key plays; false if it plays nothing. */
    [[nodiscard]] virtual bool zoneFor(int key, Zone& zone) const noexcept = 0;

    /** Voices allowed now (1 to maximumVoices), the bend range in semitones and the
        output gain. */
    [[nodiscard]] virtual int polyphonyNow() const noexcept = 0;
    [[nodiscard]] virtual double bendRangeNow() const noexcept = 0;
    [[nodiscard]] virtual float levelNow() const noexcept = 0;

    double sampleRate = 48000.0;

private:
    struct Voice
    {
        bool active = false;
        bool held = false;
        bool sustained = false;
        int key = 60;
        int channel = 0;
        std::int32_t noteId = -1;
        std::uint64_t order = 0;
        float velocityGain = 1.0f;

        Zone zone;
        const Sample* sample = nullptr;
        double position = 0.0; // in level-0 samples
        double direction = 1.0;
        dsp::Envelope amp;
        float fade = 1.0f;
        float fadeStep = 0.0f;
    };

    void tick(const ProcessBlock& block, int sample) noexcept;
    void render(float* const* channels, int numChannels, int start, int length) noexcept;
    void renderVoice(Voice& voice, float* left, float* right, int length) noexcept;
    void handle(const midi::Event& event) noexcept;
    void noteOn(const midi::Event& event) noexcept;
    void noteOff(const midi::Event& event) noexcept;
    void fadeOut(Voice& voice, double seconds) noexcept;
    int takeVoice() noexcept;

    const dsp::SincInterpolator* interpolator = nullptr;
    std::array<Voice, maximumVoices + spareVoices> voices{};
    std::span<const Sample* const> blockSamples;
    std::array<float, controlInterval> leftScratch{};
    std::array<float, controlInterval> rightScratch{};
    std::uint64_t clock = 0;
    std::uint64_t notesStarted = 0;
    double bend = 0.0; // -1 to 1
    bool sustainDown = false;
    double volume = 1.0;
    float cc7 = 1.0f;
    float cc11 = 1.0f;
};

} // namespace daw::engine::fx
