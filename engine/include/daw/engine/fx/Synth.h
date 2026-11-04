#pragma once

#include "daw/engine/dsp/BandLimitedWaves.h"
#include "daw/engine/dsp/Envelope.h"
#include "daw/engine/fx/Effect.h"

#include <array>
#include <cstdint>

namespace daw::engine::fx
{

/**
    A polyphonic subtractive synthesiser.

    - Two oscillators (sine, triangle, saw, pulse with its width) and noise, read from
      band-limited tables (dsp::BandLimitedWaves): no harmonic reaches past 0.45 of
      the sample rate, so they do not alias.
    - A state-variable filter (trapezoidal, linear: low-pass 12 or 24 dB/oct,
      band-pass, high-pass) with resonance up to Q 20 (it never oscillates on its
      own), its cutoff following the keys, envelope 2, the velocity and the LFO.
    - Envelope 1 shapes the amplitude, envelope 2 the filter and the pitch (attack a
      straight line to the peak in its time; decay and release 60 dB in theirs).
    - One LFO for every voice (sine, triangle, saw, square, random steps; free in Hz
      or at a note value of the tempo) to the pitch, the cutoff, the level and the
      pulse width; the mod wheel adds vibrato.
    - Glide from the last note played; poly, mono (envelopes start again) or legato
      (they go on) voices; 1 to 32 of them, preallocated (no allocation at a note-on).
      When every voice is taken, a new note takes the quietest releasing voice, or
      else the oldest; the stolen sound fades out over 3 ms on a spare voice while
      the new note starts on time. A key struck again takes the voice it sounds on.
    - Pitch bend (its range a parameter), the mod wheel, sustain (CC64), volume (CC7)
      and expression (CC11) are followed.

    Everything is computed per sample or every controlInterval samples counted from
    the last reset(), never per host block: the output does not depend on how the
    host cuts the timeline into blocks.
*/
class Synth final : public Effect
{
public:
    static constexpr const char* typeId = "synth";
    static constexpr int maximumVoices = 32;
    static constexpr int spareVoices = 4;
    static constexpr int controlInterval = 16;
    static constexpr double stealFadeSeconds = 0.003;

    enum Parameter
    {
        osc1Wave,
        osc1Octave,
        osc1Semitone,
        osc1Fine,
        osc1Width,
        osc1Level,
        osc2Wave,
        osc2Octave,
        osc2Semitone,
        osc2Fine,
        osc2Width,
        osc2Level,
        noiseLevel,
        filterType,
        cutoff,
        resonance,
        keyTracking,
        filterEnvelope,
        filterVelocity,
        ampAttack,
        ampDecay,
        ampSustain,
        ampRelease,
        ampVelocity,
        modAttack,
        modDecay,
        modSustain,
        modRelease,
        modToPitch,
        lfoWave,
        lfoSync,
        lfoRate,
        lfoNote,
        lfoToPitch,
        lfoToCutoff,
        lfoToLevel,
        lfoToWidth,
        wheelToVibrato,
        voiceMode,
        polyphony,
        glide,
        bendRange,
        level,
        numParameters
    };

    enum class Wave
    {
        sine,
        triangle,
        saw,
        pulse
    };

    enum class FilterType
    {
        lowPass12,
        lowPass24,
        bandPass,
        highPass
    };

    enum class VoiceMode
    {
        poly,
        mono,
        legato
    };

    Synth();

    [[nodiscard]] const EffectDescription& getDescription() const noexcept override;
    void prepare(const ProcessSetup& setup) override;
    void reset() noexcept override;
    void process(const ProcessBlock& block) noexcept override;
    [[nodiscard]] double getTailSeconds(std::span<const float> values) const override;

    /** The thread that processes: voices sounding now (spare voices fading out too). */
    [[nodiscard]] int getActiveVoices() const noexcept;

private:
    struct Filter
    {
        double ic1 = 0.0;
        double ic2 = 0.0;
    };

    struct Voice
    {
        bool active = false;
        bool held = false;      // its key is down
        bool sustained = false; // its key is up, the pedal down
        int key = 60;
        int channel = 0;
        std::int32_t noteId = -1;
        float velocity = 1.0f;
        std::uint64_t order = 0; // when it started: lower is older

        double pitch = 60.0; // semitones, gliding towards key
        double glideFrom = 60.0;
        int glideDone = 0;
        int glideLength = 0;

        double phase1 = 0.0;
        double phase2 = 0.0;
        double increment1 = 0.0;
        double increment2 = 0.0;
        double incrementStep1 = 0.0;
        double incrementStep2 = 0.0;
        dsp::BandLimitedWaves::Selection selection1;
        dsp::BandLimitedWaves::Selection selection2;
        float width1 = 0.5f;
        float width2 = 0.5f;

        dsp::Envelope amp;
        dsp::Envelope mod;
        Filter stage1;
        Filter stage2;
        double g = 0.1;
        double gStep = 0.0;
        double k = 1.4;
        bool filterStarted = false;

        std::uint32_t noise = 1;
        float fade = 1.0f; // a stolen voice fading out
        float fadeStep = 0.0f;
    };

    // Parameter values (in their units) at a control tick.
    struct Controls
    {
        std::array<float, numParameters> values{};
        [[nodiscard]] float operator[](Parameter p) const noexcept
        {
            return values[static_cast<std::size_t>(p)];
        }
    };

    void readControls(const ProcessBlock& block, int sample) noexcept;
    void controlTick(const TimeInfo& time) noexcept;
    void voiceControl(Voice& voice, int length) noexcept;
    void render(float* const* channels, int numChannels, int start, int length) noexcept;
    void renderVoice(Voice& voice, float* out, int length) noexcept;
    void handle(const midi::Event& event) noexcept;
    void noteOn(const midi::Event& event) noexcept;
    void noteOff(const midi::Event& event) noexcept;
    void startVoice(Voice& voice, const midi::Event& event, bool retrigger) noexcept;
    void moveMonoVoice(Voice& voice, int key, std::int32_t noteId, bool retrigger) noexcept;
    void releaseVoice(Voice& voice) noexcept;
    void setSustain(bool down) noexcept;
    int takeVoice() noexcept;
    void stealInto(Voice& voice) noexcept;
    [[nodiscard]] int polyphonyNow() const noexcept;
    [[nodiscard]] VoiceMode modeNow() const noexcept;

    EffectDescription description;
    double sampleRate = 48000.0;
    const dsp::BandLimitedWaves* waves = nullptr;

    std::array<Voice, maximumVoices + spareVoices> voices{};
    std::array<float, 512> mixScratch{};
    Controls controls;
    std::uint64_t clock = 0; // samples since reset
    std::uint64_t lastTick = 0; // the clock of the last control tick
    std::uint64_t notesStarted = 0;

    // Global modulation, at control ticks.
    double lfoPhase = 0.0;
    float lfoValue = 0.0f;
    float lfoRandom = 0.0f;
    std::uint32_t lfoNoise = 0x12345678u;
    float bend = 0.0f;       // -1 to 1
    float wheel = 0.0f;      // 0 to 1
    float pressure = 0.0f;
    bool sustainDown = false;
    double volume = 1.0;     // CC7 and CC11, smoothed
    double volumeTarget = 1.0;
    float cc7 = 1.0f;
    float cc11 = 1.0f;
    double lastPitch = -1.0; // the last note played, for glide

    // Mono and legato: the keys held, oldest first.
    std::array<std::uint8_t, 32> heldKeys{};
    std::array<std::int32_t, 32> heldIds{};
    int numHeld = 0;
};

} // namespace daw::engine::fx
