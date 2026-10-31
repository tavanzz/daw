#pragma once

#include "daw/engine/TimelineFade.h"

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

/** What the transport plays: anything that can render a stretch of the timeline. */
class TimelineRenderer
{
public:
    /** Audio thread. Overwrites samples [outputStart, outputStart + numSamples) of
        output with timeline samples [timelineStart, timelineStart + numSamples),
        the timeline's audio faded by fade. */
    virtual void renderTimeline(juce::int64 timelineStart, juce::AudioBuffer<float>& output,
                                int outputStart, int numSamples,
                                const TimelineFade& fade) noexcept = 0;

    /** Audio thread. Overwrites samples [outputStart, outputStart + numSamples) of
        output while the timeline is not playing: silence, unless the renderer has
        something live to play (monitored inputs). */
    virtual void renderStopped(juce::AudioBuffer<float>& output, int outputStart,
                               int numSamples) noexcept
    {
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.clear(channel, outputStart, numSamples);
    }

protected:
    ~TimelineRenderer() = default;
};

/**
    Play, pause, stop, seek and loop over a timeline, sample by sample.

    Audio thread only: every member function is real-time safe, and the object
    must only be touched by the thread that renders (or before rendering starts).
    The engine passes it the UI's commands and calls render() once per block.

    - Playback always starts with a fade-in, and pausing, stopping or seeking
      while playing first fades out, all over fadeDurationSeconds, so none of
      them clicks. The renderer applies the fade (TimelineFade) to the timeline's
      audio. The state reported by getState() changes immediately.
    - Looping and reaching the end of the timeline are sample-exact and unfaded.
    - The return position is where playback was last started from stopped, or
      where the user last seeked to. Stop, and reaching the end without looping,
      go back there; stop while already stopped goes to 0.
*/
class Transport
{
public:
    static constexpr double fadeDurationSeconds = 0.005;

    /** Sets the fade length for this sample rate. */
    void prepare(double sampleRate) noexcept;

    /** Where the timeline ends (the end of the last clip). Playback stops there
        unless it loops. Changing it never interrupts playback. */
    void setLength(juce::int64 lengthInSamples) noexcept;

    /** Stops (fading out if needed) and goes to position 0, for a new project. */
    void reset() noexcept;

    void play() noexcept;
    void pause() noexcept;
    void stop() noexcept;

    /** Moves playback to position (0 or later; past the end is allowed). */
    void seek(juce::int64 position) noexcept;

    void setLooping(bool shouldLoop) noexcept;

    /** While recording: playback runs straight on, with no loop and no end of the
        timeline (it may start with an empty one). */
    void setLinear(bool shouldRunStraight) noexcept { linear = shouldRunStraight; }
    [[nodiscard]] bool isLinear() const noexcept { return linear; }

    /** Loops [start, end); an empty or invalid range means the whole timeline,
        [0, length). The range only applies while playback is before end. */
    void setLoopRange(juce::int64 start, juce::int64 end) noexcept;

    /**
        Overwrites samples [startSample, startSample + numSamples) of output with
        what the renderer gives for the timeline from the current position, faded
        as needed, and advances the position. While not running, the renderer's
        renderStopped().
    */
    void render(TimelineRenderer& renderer, juce::AudioBuffer<float>& output, int startSample,
                int numSamples) noexcept;

    [[nodiscard]] TransportState getState() const noexcept { return state; }
    [[nodiscard]] juce::int64 getPosition() const noexcept { return position; }
    [[nodiscard]] juce::int64 getReturnPosition() const noexcept { return returnPosition; }
    [[nodiscard]] juce::int64 getLength() const noexcept { return length; }
    [[nodiscard]] bool isLooping() const noexcept { return looping; }
    [[nodiscard]] juce::int64 getLoopStart() const noexcept;
    [[nodiscard]] juce::int64 getLoopEnd() const noexcept;
    [[nodiscard]] int getFadeLength() const noexcept { return fadeLength; }

    /** True while the timeline is being rendered, including during a fade-out. */
    [[nodiscard]] bool isRunning() const noexcept { return running; }

private:
    [[nodiscard]] bool hasLoopRange() const noexcept { return loopEnd > loopStart; }
    [[nodiscard]] int targetLevel() const noexcept { return fadingOut ? 0 : fadeLength; }
    void startFadeIn() noexcept;
    void finishFadeOut() noexcept;
    void reachEnd() noexcept;

    TransportState state = TransportState::stopped;
    juce::int64 length = 0;
    juce::int64 position = 0;
    juce::int64 returnPosition = 0;
    juce::int64 loopStart = 0; // as requested; see getLoopStart()
    juce::int64 loopEnd = 0;
    bool looping = false;
    bool linear = false;

    // Rendering: running is true while the timeline is rendered. The output gain is
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
