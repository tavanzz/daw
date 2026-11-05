#pragma once

#include "daw/engine/fx/Effect.h"
#include "daw/engine/plugins/PluginLayout.h"

#include <juce_audio_processors_headless/juce_audio_processors_headless.h>

#include <algorithm>
#include <array>
#include <atomic>
#include <bitset>
#include <memory>
#include <variant>
#include <vector>

namespace daw::engine::plugins
{

/**
    A plugin (VST3, AU, LV2, through JUCE's AudioPluginInstance) as an fx::Effect, so
    that the host runs it like a built-in effect or instrument: chains, sends, bypass,
    delay compensation, automation, MIDI, NaN guard and Panic all come from
    EffectInstance and the mixer.

    - Layout: set up once for a stereo track (setUpStereoLayout()); a mono input gets
      (L + R) / 2, a mono output goes to both channels; an instrument adds its sound
      to its input.
    - MIDI: each event reaches the plugin on its sample, as MIDI 1.0 bytes.
    - Parameters: the description lists every parameter of the plugin, normalised
      0..1, not smoothed (plugins smooth their own). The user's changes reach the
      plugin from the message thread (setParameter(), undo); the audio thread only
      sends automation: each call is cut at the block's breakpoints and, while values
      move, every 32 samples on a grid of the timeline, so a step arrives on its
      sample and the result does not depend on how the timeline is cut into blocks.
    - What the plugin changes itself (its editor, gestures, its latency, its state) is
      noted with atomics, from whatever thread the plugin calls back on, and taken on
      the message thread (takeParameterReports(), takeLatencyChanged(),
      takeStateChanged()).
    - Time: the plugin's play head reports the tempo, metre, position and transport of
      every call.
    - Cost: the time each call takes against the block's duration (takeLoad()).
    - Everything the plugin runs on the audio thread is inside rt::ForeignCode.

    Message thread: create(), prepare(), state, setParameter(), the take functions,
    destruction (an instance never runs while it is prepared or destroyed: the host
    sees to it). Audio thread: reset(), process(), panic().
*/
class PluginEffect final : public fx::Effect, private juce::AudioProcessorListener
{
public:
    /** Message thread. Takes a plugin just instantiated (not prepared) and sets up
        its layout; or why it cannot be hosted. */
    [[nodiscard]] static std::variant<std::unique_ptr<PluginEffect>, juce::String>
    create(std::unique_ptr<juce::AudioPluginInstance> instance);

    ~PluginEffect() override;

    PluginEffect(const PluginEffect&) = delete;
    PluginEffect& operator=(const PluginEffect&) = delete;

    // fx::Effect
    [[nodiscard]] const fx::EffectDescription& getDescription() const noexcept override { return description; }
    void prepare(const fx::ProcessSetup& setup) override;
    void reset() noexcept override;
    void process(const fx::ProcessBlock& block) noexcept override;
    void panic() noexcept override;
    [[nodiscard]] int getLatency() const noexcept override { return latency; }
    [[nodiscard]] double getTailSeconds(std::span<const float> parameterValues) const override;
    [[nodiscard]] juce::MemoryBlock getExtraState() const override;
    bool setExtraState(const juce::MemoryBlock& state) override;

    /** Whether prepare() was called with this format last (a moved plugin needs no
        new preparing). */
    [[nodiscard]] bool isPreparedFor(const fx::ProcessSetup& setup) const noexcept
    {
        return prepared && juce::exactlyEqual(setup.sampleRate, sampleRate) &&
               setup.maximumBlockSize == maximumBlockSize && setup.offline == offline;
    }

    /** Message thread, while no slot processes it: takes the latency the plugin
        reports now (it reported a change). */
    void refreshLatency() noexcept
    {
        latency = std::max(0, plugin->getLatencySamples());
        latencyChanged = false;
    }

    /** Message thread. The plugin itself, for its editor. */
    [[nodiscard]] juce::AudioPluginInstance& getPlugin() noexcept { return *plugin; }
    [[nodiscard]] const juce::PluginDescription& getPluginDescription() const noexcept { return pluginDescription; }
    [[nodiscard]] PluginLayout getLayout() const noexcept { return layout; }

    /** Message thread. Sets a parameter (normalised) from the host: the generic
        editor, undo. Not reported back as the plugin's own change. */
    void setParameter(int index, float normalised);
    [[nodiscard]] float getParameter(int index) const;

    /** Message thread. A parameter's value as the plugin writes it ("-6.0 dB"). */
    [[nodiscard]] juce::String getParameterText(int index, float normalised) const;

    /** A change the plugin made to one of its parameters (its editor, its own
        automation): its value now, and whether the user is still dragging it. */
    struct ParameterReport
    {
        int index = 0;
        float value = 0.0f;
        bool inGesture = false;   ///< a gesture is going on
        bool gestureEnded = false; ///< a gesture ended since the last report
    };

    /** Message thread. What the plugin changed since the last call. */
    void takeParameterReports(std::vector<ParameterReport>& reports);

    /** Message thread. Whether the plugin reported a new latency, or a change of its
        state that is not a parameter (a preset loaded in its editor), since the last
        call. */
    [[nodiscard]] bool takeLatencyChanged() noexcept { return latencyChanged.exchange(false); }
    [[nodiscard]] bool takeStateChanged() noexcept { return stateChanged.exchange(false); }

    /** How much of the real time the plugin takes, as a fraction of each block's
        duration: the average over about a second, the most of a block since the last
        call, and how many blocks it overran (took longer than their duration). */
    struct Load
    {
        float average = 0.0f;
        float peak = 0.0f;
        int overruns = 0;
    };

    /** Message thread. */
    [[nodiscard]] Load takeLoad() noexcept;

    /** The grid along which moving automation reaches the plugin, in samples. */
    static constexpr int automationGrid = 32;

private:
    struct ParameterWatch
    {
        std::atomic<float> value{0.0f};
        std::atomic<std::uint32_t> changes{0};
        std::atomic<std::uint32_t> gestures{0}; ///< odd while a gesture goes on
    };

    class PlayHead final : public juce::AudioPlayHead
    {
    public:
        juce::Optional<PositionInfo> getPosition() const override;
        fx::TimeInfo time;
        double sampleRate = 48000.0;
    };

    PluginEffect(std::unique_ptr<juce::AudioPluginInstance> pluginToHost, PluginLayout chosenLayout);

    void runPlugin(float* const* channels, int numChannels, int start, int end, bool lastPiece,
                   midi::EventSpan events, std::size_t& nextEvent) noexcept;
    void addMidi(const midi::Event& event, int offset) noexcept;
    void sendAutomation(const fx::ProcessBlock& block, int sample) noexcept;

    // juce::AudioProcessorListener (any thread)
    void audioProcessorParameterChanged(juce::AudioProcessor*, int index, float value) override;
    void audioProcessorChanged(juce::AudioProcessor*, const ChangeDetails& details) override;
    void audioProcessorParameterChangeGestureBegin(juce::AudioProcessor*, int index) override;
    void audioProcessorParameterChangeGestureEnd(juce::AudioProcessor*, int index) override;

    std::unique_ptr<juce::AudioPluginInstance> plugin;
    juce::PluginDescription pluginDescription;
    PluginLayout layout;
    bool instrument = false;
    fx::EffectDescription description;
    juce::Array<juce::AudioProcessorParameter*> parameters;

    double sampleRate = 48000.0;
    int maximumBlockSize = 512;
    int latency = 0;
    bool prepared = false;
    bool offline = false;

    // Audio thread.
    juce::AudioBuffer<float> scratch;
    juce::MidiBuffer midiBuffer;
    PlayHead playHead;
    std::vector<float> lastSent; ///< automation sent, per parameter (NaN: none yet)
    std::bitset<16 * 128> sounding; ///< notes on, by channel and key
    std::array<int, 512> cuts{};
    bool panicPending = false;

    // Reported to the message thread.
    std::unique_ptr<ParameterWatch[]> watches;
    std::vector<std::uint32_t> seenChanges;  // message thread
    std::vector<std::uint32_t> seenGestures; // message thread
    std::atomic<bool> parametersChanged{false};
    std::atomic<bool> latencyChanged{false};
    std::atomic<bool> stateChanged{false};
    std::atomic<int> hostIsSetting{-1}; ///< the parameter setParameter() is setting
    std::atomic<bool> hostIsLoading{false}; ///< prepare() or setExtraState() runs

    std::atomic<float> loadAverage{0.0f};
    std::atomic<float> loadPeak{0.0f};
    std::atomic<int> overruns{0};
    double ticksPerSecond = 1.0;
};

} // namespace daw::engine::plugins
