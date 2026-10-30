#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/Transport.h"
#include "daw/engine/dsp/PeakLevelMeter.h"
#include "daw/engine/rt/SpscQueue.h"

#include <juce_audio_basics/juce_audio_basics.h>

#include <atomic>
#include <cstdint>
#include <memory>

namespace daw::engine
{

namespace detail
{
/** A transport command queued from the message thread to the audio thread.
    (At namespace scope: nested in Engine it would not count as nothrow
    constructible until Engine is complete, which SpscQueue checks.) */
struct EngineCommand
{
    enum class Type : std::uint8_t
    {
        play,
        pause,
        stop,
        seek,
        setLooping,
        resetTransport
    };

    Type type = Type::play;
    juce::int64 value = 0;
};

/** An arrangement with the mixer state that goes with it: what the message thread
    hands to the audio thread in one piece. */
struct EngineSnapshot
{
    std::unique_ptr<Arrangement> arrangement;
    MixState mix;
};
} // namespace detail

/**
    Top-level audio engine: plays an Arrangement through a Transport and the Mixer.

    Threads:
    - Message thread: the constructor, the destructor, prepare() (while the audio
      device is stopped), setArrangement(), the transport commands and
      collectGarbage(). Commands are queued and take effect at the start of the next
      audio block, after any new arrangement.
    - Audio thread: process() only. It never allocates, frees, locks or blocks.
    - Any thread: the get...() functions, which return what the audio thread last
      published.

    A new arrangement replaces the current one as a whole, through one atomic
    pointer exchange. If it changes what the tracks play while playback is running,
    the old and the new are crossfaded over crossfadeSeconds; gain changes ramp in
    the mixer instead. Replaced arrangements go back to the message thread on a
    queue and are freed by collectGarbage().
*/
class Engine final : private TimelineRenderer
{
public:
    static constexpr double crossfadeSeconds = 0.005;

    Engine() = default;

    /** Message thread, with the audio device stopped: frees everything. */
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /** Message thread, while process() is not running (before the device starts or
        while it restarts). Call whenever the sample rate or block size changes. */
    void prepare(double newSampleRate, int newMaximumBlockSize);

    /** Audio thread. Overwrites the whole buffer with the next block of output. */
    void process(juce::AudioBuffer<float>& output) noexcept;

    /** Message thread. Replaces what is played, from the next audio block. Its
        sample rate must match the one given to prepare(), otherwise the engine
        plays silence. An arrangement the audio thread has not picked up yet is
        replaced (and freed here). */
    void setArrangement(std::unique_ptr<Arrangement> arrangement);

    /** Message thread. Transport commands, see Transport for their behaviour. */
    void play();
    void pause();
    void stop();
    void seek(juce::int64 position);
    void setLooping(bool shouldLoop);
    /** Stops and goes to 0 (for a new project). */
    void resetTransport();

    /** Message thread. Frees arrangements the audio thread no longer uses.
        setArrangement() calls it too; call it regularly (a UI timer is fine). */
    void collectGarbage();

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] int getMaximumBlockSize() const noexcept { return maximumBlockSize; }

    /** Any thread: state as of the last audio block. */
    [[nodiscard]] TransportState getTransportState() const noexcept;
    [[nodiscard]] juce::int64 getPlayheadPosition() const noexcept;
    [[nodiscard]] juce::int64 getLength() const noexcept;
    [[nodiscard]] bool isLooping() const noexcept;

    /** One reader thread (the UI): highest absolute output sample on the channel
        (0 = left, 1 = right) since the previous call, as a linear gain. This is the
        master meter: after the master volume and the transport fades. */
    [[nodiscard]] float takeOutputPeak(int channel) noexcept;

private:
    using Command = detail::EngineCommand;
    using Snapshot = detail::EngineSnapshot;

    void renderTimeline(juce::int64 timelineStart, juce::AudioBuffer<float>& output,
                        int outputStart, int numSamples) noexcept override;
    void renderPiece(juce::int64 timelineStart, juce::AudioBuffer<float>& output,
                     int outputStart, int numSamples) noexcept;

    void send(Command command);
    void apply(const Command& command) noexcept;
    void adoptPendingSnapshot() noexcept;
    void finishCrossfade() noexcept;
    void retire(Snapshot* snapshot) noexcept;
    [[nodiscard]] bool canPlay(const Snapshot* snapshot) const noexcept;
    void publish() noexcept;

    double sampleRate = 0.0;
    int maximumBlockSize = 0;
    int crossfadeLength = 1;

    // Audio thread only.
    Transport transport;
    Mixer mixer;
    Snapshot* current = nullptr;
    Snapshot* fadingOut = nullptr; // the previous snapshot, during a crossfade
    int crossfadeDone = 0;
    juce::AudioBuffer<float> crossfadeBuffer;
    juce::AudioBuffer<float> stereoBuffer; // for devices with fewer than two outputs
    Snapshot* waitingToRetire = nullptr; // only if the retired queue was full

    // Message thread -> audio thread.
    rt::SpscQueue<Command, 256> commands;
    std::atomic<Snapshot*> pendingSnapshot{nullptr};

    // Audio thread -> message thread: snapshots to free.
    rt::SpscQueue<Snapshot*, 256> retired;

    // Audio thread -> any thread.
    std::atomic<TransportState> publishedState{TransportState::stopped};
    std::atomic<juce::int64> publishedPosition{0};
    std::atomic<juce::int64> publishedLength{0};
    std::atomic<bool> publishedLooping{false};
    dsp::PeakLevelMeter outputMeter;
};

} // namespace daw::engine
