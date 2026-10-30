#pragma once

#include <juce_audio_basics/juce_audio_basics.h>

#include <cstdint>

namespace daw::engine
{

enum class TransportState : std::uint8_t
{
    stopped,
    playing,
    paused
};

/**
    Play, pause, stop, seek and loop for one source, sample by sample.

    Audio thread only: every member function is real-time safe, and the object
    must only be touched by the thread that renders (or before rendering starts).
    The engine passes it the UI's commands and calls render() once per block.

    - Playback always starts with a fade-in, and pausing, stopping or seeking
      while playing first fades out, all over fadeDurationSeconds, so none of
      them clicks. The state reported by getState() changes immediately.
    - Looping and reaching the end of the source are sample-exact and unfaded.
    - Stop, and reaching the end of the source without looping, return the
      position to 0.
*/
class Transport
{
public:
    static constexpr double fadeDurationSeconds = 0.005;

    /** Sets the fade length for this sample rate. */
    void prepare(double sampleRate) noexcept;

    /** A new source: stopped, at position 0, looping the whole source. */
    void setSourceLength(juce::int64 lengthInSamples) noexcept;

    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;

    /** Moves playback to position, clamped to the source. */
    void seek(juce::int64 position) noexcept;

    void setLooping(bool shouldLoop) noexcept;

    /** Loops [start, end) instead of the whole source; an empty or invalid range
        means the whole source. The range only applies while playback is before end. */
    void setLoopRange(juce::int64 start, juce::int64 end) noexcept;

    /**
        Overwrites samples [startSample, startSample + numSamples) of output with the
        source from the current position, and advances the position.

        Output channels 0 and 1 get the source's first two channels (a mono source
        on both); any further output channels are cleared. The source must hold the
        number of samples given to setSourceLength().
    */
    void render(const juce::AudioBuffer<float>& source, juce::AudioBuffer<float>& output,
                int startSample, int numSamples) noexcept;

    [[nodiscard]] TransportState getState() const noexcept { return state; }
    [[nodiscard]] juce::int64 getPosition() const noexcept { return position; }
    [[nodiscard]] bool isLooping() const noexcept { return looping; }
    [[nodiscard]] juce::int64 getLoopStart() const noexcept { return loopStart; }
    [[nodiscard]] juce::int64 getLoopEnd() const noexcept { return loopEnd; }
    [[nodiscard]] int getFadeLength() const noexcept { return fadeLength; }

    /** True while the source is being read, including during a fade-out. */
    [[nodiscard]] bool isRunning() const noexcept { return running; }

private:
    [[nodiscard]] int targetLevel() const noexcept { return fadingOut ? 0 : fadeLength; }
    void startFadeIn() noexcept;
    void finishFadeOut() noexcept;
    void reachEnd() noexcept;
    void writeSegment(const juce::AudioBuffer<float>& source, juce::AudioBuffer<float>& output,
                      int outputStart, int length) noexcept;

    TransportState state = TransportState::stopped;
    juce::int64 sourceLength = 0;
    juce::int64 position = 0;
    juce::int64 loopStart = 0;
    juce::int64 loopEnd = 0;
    bool looping = false;

    // Rendering: running is true while samples are read. The output gain is
    // level / fadeLength; an integer level makes every fade exact and repeatable.
    bool running = false;
    bool fadingOut = false;
    int level = 0;
    int fadeLength = 1;

    // A seek requested while running waits for the fade-out to finish.
    bool hasPendingSeek = false;
    juce::int64 seekTarget = 0;
};

} // namespace daw::engine
