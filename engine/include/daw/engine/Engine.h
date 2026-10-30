#pragma once

#include "daw/engine/AudioData.h"
#include "daw/engine/Transport.h"
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
        setLooping
    };

    Type type = Type::play;
    juce::int64 value = 0;
};
} // namespace detail

/**
    Top-level audio engine: plays one AudioData through a Transport.

    Threads:
    - Message thread: the constructor, the destructor, prepare() (while the audio
      device is stopped), setAudio(), the transport commands and collectGarbage().
      Commands are queued and take effect at the start of the next audio block.
    - Audio thread: process() only. It never allocates, frees, locks or blocks.
    - Any thread: the get...() functions, which return what the audio thread last
      published.
*/
class Engine
{
public:
    Engine() = default;

    /** Message thread, with the audio device stopped: frees all audio data. */
    ~Engine();

    Engine(const Engine&) = delete;
    Engine& operator=(const Engine&) = delete;

    /** Message thread, while process() is not running (before the device starts or
        while it restarts). Call whenever the sample rate or block size changes. */
    void prepare(double newSampleRate, int newMaximumBlockSize);

    /** Audio thread. Overwrites the whole buffer with the next block of output. */
    void process(juce::AudioBuffer<float>& output) noexcept;

    /** Message thread. Replaces the audio being played. If something is playing,
        it is faded out first; the new audio starts stopped at position 0. Its
        sample rate must match the one given to prepare(), otherwise the engine
        plays silence. */
    void setAudio(std::unique_ptr<AudioData> audio);

    /** Message thread. Transport commands, see Transport for their behaviour. */
    void play();
    void pause();
    void stop();
    void seek(juce::int64 position);
    void setLooping(bool shouldLoop);

    /** Message thread. Frees audio data the audio thread no longer uses. setAudio()
        calls it too; call it regularly (a UI timer is fine). */
    void collectGarbage();

    [[nodiscard]] double getSampleRate() const noexcept { return sampleRate; }
    [[nodiscard]] int getMaximumBlockSize() const noexcept { return maximumBlockSize; }

    /** Any thread: state as of the last audio block. */
    [[nodiscard]] TransportState getTransportState() const noexcept;
    [[nodiscard]] juce::int64 getPlayheadPosition() const noexcept;
    [[nodiscard]] juce::int64 getAudioLength() const noexcept;
    [[nodiscard]] bool isLooping() const noexcept;

private:
    using Command = detail::EngineCommand;

    void send(Command command);
    void apply(const Command& command) noexcept;
    void adoptPendingAudio() noexcept;
    void retire(AudioData* audio) noexcept;
    void publish() noexcept;

    double sampleRate = 0.0;
    int maximumBlockSize = 0;

    // Audio thread only.
    Transport transport;
    AudioData* current = nullptr;
    AudioData* waitingToRetire = nullptr; // only if the retired queue was full

    // Message thread -> audio thread.
    rt::SpscQueue<Command, 256> commands;
    std::atomic<AudioData*> pendingAudio{nullptr};

    // Audio thread -> message thread: audio to free.
    rt::SpscQueue<AudioData*, 32> retired;

    // Audio thread -> any thread.
    std::atomic<TransportState> publishedState{TransportState::stopped};
    std::atomic<juce::int64> publishedPosition{0};
    std::atomic<juce::int64> publishedLength{0};
    std::atomic<bool> publishedLooping{false};
};

} // namespace daw::engine
