#pragma once

#include "daw/engine/Arrangement.h"
#include "daw/engine/AudioInputs.h"
#include "daw/engine/Mixer.h"
#include "daw/engine/Recorder.h"
#include "daw/engine/Transport.h"
#include "daw/engine/dsp/InputMeters.h"
#include "daw/engine/dsp/Metronome.h"
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
        resetTransport,
        countIn
    };

    Type type = Type::play;
    juce::int64 value = 0;
    juce::int64 length = 0;
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

    Tracks that monitor their input are heard whatever the transport does: the
    mixer runs while stopped too, and the transport's fades only touch the clips.

    The metronome clicks on the arrangement's beats while playing (if it is on) and
    during a count-in: bars of clicks before playback starts, the timeline silent.
    Clicks are added after the master meter: they are not part of the mix.

    Recording (see Recorder) plays from a start position, after an optional count-in,
    and captures the targets' inputs aligned to the sample with what was played.
    While it runs the transport goes straight on: loop and the end of the timeline
    do not apply, seeks are ignored, pause stops.
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

    /** Audio thread. Takes the device's input for this block (as many samples per
        channel as output has) and overwrites the whole output buffer with the next
        block of output. */
    void process(const AudioInputs& inputs, juce::AudioBuffer<float>& output) noexcept;

    /** Audio thread. A block without any input. */
    void process(juce::AudioBuffer<float>& output) noexcept { process(AudioInputs{}, output); }

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

    /** Message thread. From stopped: clicks for countInLength samples, as if the
        timeline ran from position - countInLength, then plays from position (the
        return position, as play()). While playing, plays from position without a
        count-in. Stop, pause, seek and reset cancel it. */
    void playWithCountIn(juce::int64 position, juce::int64 countInLength);

    /** Message thread. Records setup's targets (see RecordingSetup), starting as
        soon as the transport is stopped, until stop() or pause(). setup must stay
        alive, unchanged, until getRecordingStatus() is complete. Not while another
        recording is in progress. */
    void startRecording(const RecordingSetup& setup);

    /** Any thread. */
    [[nodiscard]] RecordingStatus getRecordingStatus() const noexcept;

    /** Message thread. Measures latency (see LatencyTest) as soon as the transport is
        stopped and nothing records: while it runs, the output carries only the test
        signal (no monitoring, no playback, so a cable cannot feed back). test must
        stay alive, unchanged, until isLatencyTestDone(). */
    void startLatencyTest(LatencyTest& test);

    /** Any thread. */
    [[nodiscard]] bool isLatencyTestDone() const noexcept;
    [[nodiscard]] float getLatencyTestProgress() const noexcept;

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
    [[nodiscard]] bool isCountingIn() const noexcept;

    /** One reader thread (the UI): highest absolute output sample on the channel
        (0 = left, 1 = right) since the previous call, as a linear gain. This is the
        master meter: after the master volume and the transport fades. */
    [[nodiscard]] float takeOutputPeak(int channel) noexcept;

    /** One reader thread (the UI): highest absolute sample on the device input
        channel since the previous call, as a linear gain, before any processing. */
    [[nodiscard]] float takeInputPeak(int channel) noexcept;

private:
    using Command = detail::EngineCommand;
    using Snapshot = detail::EngineSnapshot;

    void renderTimeline(juce::int64 timelineStart, juce::AudioBuffer<float>& output,
                        int outputStart, int numSamples, const TimelineFade& fade) noexcept override;
    void renderStopped(juce::AudioBuffer<float>& output, int outputStart,
                       int numSamples) noexcept override;
    void renderBlock(juce::AudioBuffer<float>& output, int numSamples) noexcept;
    void renderCountIn(juce::AudioBuffer<float>& output, int outputStart,
                       int numSamples) noexcept;
    void renderPiece(juce::int64 timelineStart, juce::AudioBuffer<float>& output,
                     int outputStart, int numSamples, const MixContext& context) noexcept;
    void finishPiece(juce::AudioBuffer<float>& output, int outputStart, int numSamples,
                     juce::int64 timelineStart, bool newClicks) noexcept;
    [[nodiscard]] MixContext makeContext(int outputStart) const noexcept;
    void startPendingRecording() noexcept;
    bool runLatencyTest(const AudioInputs& inputs, juce::AudioBuffer<float>& output) noexcept;
    void endRecording() noexcept;
    [[nodiscard]] bool autoMonitorsAt(juce::int64 timelinePosition) const noexcept;
    [[nodiscard]] int pieceLength(juce::int64 timelinePosition, int available) const noexcept;

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
    dsp::Metronome metronome;
    juce::int64 countInRemaining = 0; // samples of count-in still to click
    juce::int64 countInPosition = 0;  // the timeline position the count-in stands for
    int meteredChannels = 2;          // the device's outputs, as far as the meter goes
    Recorder recorder;
    juce::int64 streamPosition = 0;   // samples processed since the engine started
    bool recordingTransport = false;  // the transport runs linear for a recording
    LatencyTest* latencyTest = nullptr;
    std::size_t latencyTestPosition = 0;
    Snapshot* current = nullptr;
    Snapshot* fadingOut = nullptr; // the previous snapshot, during a crossfade
    int crossfadeDone = 0;
    juce::AudioBuffer<float> crossfadeBuffer;
    juce::AudioBuffer<float> stereoBuffer; // for devices with fewer than two outputs

    // The current block's inputs, and where the buffer being rendered starts in it
    // (devices with fewer than two outputs are rendered in pieces).
    const AudioInputs* blockInputs = nullptr;
    int renderOffset = 0;
    Snapshot* waitingToRetire = nullptr; // only if the retired queue was full

    // Message thread -> audio thread.
    rt::SpscQueue<Command, 256> commands;
    std::atomic<Snapshot*> pendingSnapshot{nullptr};
    std::atomic<const RecordingSetup*> pendingRecording{nullptr};
    std::atomic<LatencyTest*> pendingLatencyTest{nullptr};
    std::atomic<bool> latencyTestDone{false};
    std::atomic<float> latencyTestProgress{0.0f};

    // Audio thread -> message thread: snapshots to free.
    rt::SpscQueue<Snapshot*, 256> retired;

    // Audio thread -> any thread.
    std::atomic<TransportState> publishedState{TransportState::stopped};
    std::atomic<juce::int64> publishedPosition{0};
    std::atomic<juce::int64> publishedLength{0};
    std::atomic<bool> publishedLooping{false};
    std::atomic<bool> publishedCountingIn{false};
    dsp::PeakLevelMeter outputMeter;
    dsp::InputMeters inputMeters;
};

} // namespace daw::engine
