#pragma once

#include "daw/engine/AudioInputs.h"
#include "daw/engine/rt/AudioFifo.h"

#include <juce_core/juce_core.h>

#include <atomic>
#include <cstdint>
#include <vector>

namespace daw::engine
{

/** What one recording records. Built on the message thread and handed to the
    engine whole; it must outlive the recording (until its state is complete). */
struct RecordingSetup
{
    /** One track being recorded: its input channels go to its FIFO. */
    struct Target
    {
        juce::int64 trackId = 0;
        int inputChannel = 0;     ///< first device input channel
        int numInputChannels = 1; ///< 1 or 2; the FIFO has as many channels
        rt::AudioFifo* fifo = nullptr;
    };

    std::vector<Target> targets;

    /** Where playback, and so the recording, starts on the timeline. */
    juce::int64 startPosition = 0;

    /** Samples of count-in before it (0 for none). */
    juce::int64 countInLength = 0;

    /** Samples from the moment the engine plays a sample to the moment the device
        delivers what it caused at an input: output plus input latency. */
    juce::int64 latency = 0;

    /** With punchOut > punchIn, automatic monitoring hears the inputs only inside
        [punchIn, punchOut): what the recording will keep. */
    juce::int64 punchIn = 0;
    juce::int64 punchOut = 0;
};

/** A latency measurement: the engine plays signal on one output and captures one
    input from the same sample on. Built on the message thread and handed to the
    engine whole; it must outlive the measurement (until the engine reports done). */
struct LatencyTest
{
    int outputChannel = 0; ///< 0 (left) or 1 (right)
    int inputChannel = 0;  ///< a device input channel
    const std::vector<float>* signal = nullptr;
    std::vector<float> captured; ///< sized by the caller: how much input to capture
};

enum class RecordingState : std::uint8_t
{
    idle,      ///< no recording yet
    starting,  ///< handed to the engine, which starts it as soon as the transport is stopped
    recording, ///< counting in or playing; inputs are captured
    finishing, ///< stopped; capturing the last latency's worth of input
    complete   ///< everything captured is in the FIFOs; the setup is free
};

/** What the engine reports about the current or last recording. */
struct RecordingStatus
{
    RecordingState state = RecordingState::idle;

    /** Timeline position of the first sample captured: the start of the count-in.
        Sample k of every FIFO belongs at firstPosition + k. */
    juce::int64 firstPosition = 0;

    /** Samples (per channel) captured into every FIFO so far. */
    juce::int64 samplesCaptured = 0;

    /** Where on the timeline the transport was stopped (once finishing). */
    juce::int64 stopPosition = 0;

    /** Capture ended early: a FIFO was full (the disk could not keep up). */
    bool overrun = false;

    /** Capture ended early: the audio device stopped or restarted. */
    bool interrupted = false;
};

/**
    The audio-thread side of recording: copies device inputs into the targets' FIFOs
    so that every captured sample lines up with what was being played.

    Positions are stream indices, the engine's count of samples since it started.
    The recording runs from stream index s0 (where its count-in or playback starts,
    at timeline position firstPosition) to s1 (where it is stopped); the transport
    runs straight on in between. What was played at stream index s comes back to an
    input at s + latency, so the recorder captures the inputs [s0 + latency,
    s1 + latency): sample k of the capture belongs at timeline position
    firstPosition + k, exactly.

    All targets capture the same samples: if any FIFO lacks room for a block, the
    capture stops for all of them (an overrun), never blocking.

    begin(), end() and capture(): audio thread. interrupt(): message thread, while the
    audio thread is not running. getStatus(): any thread.
*/
class Recorder
{
public:
    void begin(const RecordingSetup& setup, juce::int64 streamStart) noexcept;
    void end(juce::int64 streamStop) noexcept;

    /** Audio thread: a recording stopped before it started. It completes having
        captured nothing. */
    void cancel(const RecordingSetup& setup) noexcept;
    void capture(const AudioInputs& inputs, juce::int64 blockStart, int numSamples) noexcept;
    void interrupt() noexcept;

    /** Message thread: a setup was handed to the engine. */
    void markStarting() noexcept
    {
        state.store(RecordingState::starting, std::memory_order_release);
    }

    /** Audio thread: counting in or playing, not yet stopped. */
    [[nodiscard]] bool isRecording() const noexcept { return setup != nullptr && !stopped; }

    /** Audio thread: the punch range of the recording in progress, if it has one. */
    [[nodiscard]] const RecordingSetup* getSetup() const noexcept { return setup; }

    [[nodiscard]] RecordingStatus getStatus() const noexcept;

private:
    void complete() noexcept;

    // Audio thread only.
    const RecordingSetup* setup = nullptr;
    juce::int64 captureStart = 0;
    juce::int64 captureEnd = 0;
    bool stopped = false;

    // Published.
    std::atomic<RecordingState> state{RecordingState::idle};
    std::atomic<juce::int64> firstPosition{0};
    std::atomic<juce::int64> captured{0};
    std::atomic<juce::int64> stopPosition{0};
    std::atomic<bool> overrun{false};
    std::atomic<bool> interrupted{false};
};

} // namespace daw::engine
