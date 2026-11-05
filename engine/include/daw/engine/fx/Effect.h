#pragma once

#include "daw/engine/Sample.h"
#include "daw/engine/fx/Parameter.h"
#include "daw/engine/midi/MidiEvent.h"

#include <juce_core/juce_core.h>

#include <span>
#include <vector>

namespace daw::engine::fx
{

/** What an effect is and which parameters it has. Fixed for the effect's life. */
struct EffectDescription
{
    juce::String type; ///< saved in projects ("equaliser", "compressor"...); never changes
    juce::String name; ///< shown to the user
    std::vector<ParameterInfo> parameters;

    /** An instrument plays the notes it receives (ProcessBlock::events) and adds its
        sound to its input (silence, unless an instrument being replaced fades out
        before it). It sits first in a MIDI track's chain. */
    bool isInstrument = false;

    /** The keys of the files it plays (a sampler's "sample", a drum sampler's pads),
        which the host loads and hands over with every block (ProcessBlock::samples). */
    std::vector<juce::String> sampleSlots;

    /** -1 if there is no parameter with that id. */
    [[nodiscard]] int indexOf(const juce::String& parameterId) const noexcept;
};

/** The format an effect is prepared for. */
struct ProcessSetup
{
    double sampleRate = 48000.0;
    int maximumBlockSize = 512; ///< process() never gets more samples
    int numChannels = 2;
    bool offline = false; ///< rendering faster than real time (an export): plugins may take their time
};

/** Where the audio of a block is on the timeline, and the project's tempo: for
    tempo-synced effects (and, later, plugins). */
struct TimeInfo
{
    double tempo = 120.0; ///< quarter notes per minute
    int numerator = 4;
    int denominator = 4;
    juce::int64 position = 0; ///< timeline sample of the block's first sample
    bool playing = false;     ///< whether the timeline runs
};

/**
    The values of an effect's parameters over one block, in their own units.

    A parameter either keeps one value for the whole block (getChanging() is null)
    or changes from sample to sample (a ramp after the user moved it, or
    automation): then getChanging() gives every sample's value. Values are given
    for each sample so that automation is exact and the result never depends on
    how the timeline is cut into blocks.
*/
class ParameterValues
{
public:
    ParameterValues() = default;
    ParameterValues(const float* lastValues, const float* const* changingValues,
                    int numParameters) noexcept
        : last(lastValues)
        , changing(changingValues)
        , count(numParameters)
    {
    }

    [[nodiscard]] int size() const noexcept { return count; }

    /** The value of the block's last sample. */
    [[nodiscard]] float get(int index) const noexcept { return last[index]; }

    /** The value of every sample of the block, or null if it is get() throughout. */
    [[nodiscard]] const float* getChanging(int index) const noexcept { return changing[index]; }

    [[nodiscard]] float at(int index, int sample) const noexcept
    {
        const float* values = changing[index];
        return values != nullptr ? values[sample] : last[index];
    }

private:
    const float* last = nullptr;
    const float* const* changing = nullptr;
    int count = 0;
};

/** One block for Effect::process(): audio processed in place. */
struct ProcessBlock
{
    float* const* channels = nullptr;
    int numChannels = 0;
    int numSamples = 0;
    ParameterValues parameters;
    TimeInfo time;

    /** For instruments: the block's MIDI events, sorted, offsets within
        [0, numSamples). Every note-on is followed, in this block or a later one, by
        its note-off, unless the effect is reset first. Empty for effects. */
    midi::EventSpan events;

    /** One per EffectDescription::sampleSlots (null where no file is loaded): valid
        for this block. A pointer different from the last block's means the old
        sample may be gone: stop reading it at once. */
    std::span<const Sample* const> samples;

    /** The samples, within (0, numSamples), where an automated parameter's curve has
        a point or the timeline jumps, sorted: where a value may step. Plugin adapters
        cut their calls there, so that the plugin gets each step on its sample. */
    std::span<const int> breakpoints;
};

/**
    An audio effect or an instrument. The built-in effects and instruments implement
    it, and so will the adapters for plugin formats (VST3, AU, CLAP): nothing here
    belongs to one of them.

    - The host owns the parameter values. Every block it hands them over, already
      smoothed and automated (see ParameterValues); an effect never needs its own
      parameter smoothing, and plugin adapters turn them into the format's
      sample-accurate events. Formats that think in 0..1 use
      ParameterInfo::toNormalised().
    - Threads: the constructor, prepare(), getTailSeconds() and the state functions
      run on the message thread (or an export's thread); reset() and process() on
      the audio thread, real-time safe. Never both at once.
    - Latency: how many samples the output lags behind the input (a limiter's
      lookahead). It is fixed between two prepare() calls; the host delays every
      other path to match.
    - Instruments (EffectDescription::isInstrument) receive MIDI events with every
      block, each at its sample (ProcessBlock::events), as VST3 event lists, CLAP's
      note and MIDI events and AU's sample-offset MIDI events carry them. The host
      keeps the stream well formed: a note-off only follows its own note-on, every
      held note gets its note-off (bypass, stop, jumps), and reset() forgets every
      note. Instruments never rely on "all notes off" controllers. Their tail is
      their release.
*/
class Effect
{
public:
    virtual ~Effect() = default;

    [[nodiscard]] virtual const EffectDescription& getDescription() const noexcept = 0;

    /** Allocates what processing needs and clears the state. */
    virtual void prepare(const ProcessSetup& setup) = 0;

    /** Audio thread. Clears the state (tails, envelopes) as if just prepared. */
    virtual void reset() noexcept = 0;

    /** Audio thread. Processes block.numSamples samples of each channel in place. */
    virtual void process(const ProcessBlock& block) noexcept = 0;

    /** Audio thread, instruments only: Panic. All notes off and all sound off: every
        voice stops at once and the next process() starts with no note held (the
        host forgets them too). A plugin adapter also sends the format's "all notes
        off" and "all sound off". By default reset(). */
    virtual void panic() noexcept { reset(); }

    /** Samples the output lags behind the input, at the prepared sample rate. */
    [[nodiscard]] virtual int getLatency() const noexcept { return 0; }

    /** How long the effect may go on sounding after its input falls silent, with
        these parameter values (in their units, one per parameter); infinity for an
        endless tail. */
    [[nodiscard]] virtual double getTailSeconds(std::span<const float> parameterValues) const
    {
        juce::ignoreUnused(parameterValues);
        return 0.0;
    }

    /** State beyond the parameter values, for projects (a plugin's own data);
        empty for the built-in effects. */
    [[nodiscard]] virtual juce::MemoryBlock getExtraState() const { return {}; }
    virtual bool setExtraState(const juce::MemoryBlock& state)
    {
        return state.isEmpty();
    }
};

} // namespace daw::engine::fx
