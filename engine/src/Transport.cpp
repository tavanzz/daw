#include "daw/engine/Transport.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{

void Transport::prepare(double sampleRate) noexcept
{
    jassert(sampleRate > 0.0);
    fadeLength = std::max(1, static_cast<int>(std::lround(fadeDurationSeconds * sampleRate)));

    // A device restart interrupts any fade: settle it at once.
    if (running && fadingOut)
        finishFadeOut();
    else
        level = running ? fadeLength : 0;
}

void Transport::setLength(juce::int64 lengthInSamples) noexcept
{
    jassert(lengthInSamples >= 0);
    length = std::max(juce::int64{0}, lengthInSamples);
}

void Transport::reset() noexcept
{
    state = TransportState::stopped;
    returnPosition = 0;
    hasPendingSeek = false;

    if (running)
        fadingOut = true; // lands on 0 when the fade-out ends
    else
        position = 0;
}

void Transport::play() noexcept
{
    if (length == 0 || state == TransportState::playing)
        return;

    const bool wasStopped = state == TransportState::stopped;
    state = TransportState::playing;

    if (!running)
    {
        if (wasStopped)
            returnPosition = position;
        startFadeIn();
    }
    else if (wasStopped)
    {
        // Still fading out after stop(): finish stopping, then start again from
        // where the stop is taking playback.
        seekTarget = returnPosition;
        hasPendingSeek = true;
        fadingOut = true;
    }
    else if (!hasPendingSeek)
    {
        fadingOut = false; // reverse a pause fade from wherever it got to
    }
}

void Transport::pause() noexcept
{
    if (state != TransportState::playing)
        return;

    state = TransportState::paused;
    fadingOut = true;
}

void Transport::stop() noexcept
{
    if (state == TransportState::stopped && !running)
    {
        position = 0;
        returnPosition = 0;
        return;
    }

    const bool wasPlaying = state == TransportState::playing;
    state = TransportState::stopped;
    hasPendingSeek = false;

    if (wasPlaying)
        fadingOut = true; // the position goes back when the fade-out ends
    else if (!running)
        position = returnPosition;
}

void Transport::seek(juce::int64 newPosition) noexcept
{
    newPosition = std::max(juce::int64{0}, newPosition);
    returnPosition = newPosition;

    if (running)
    {
        seekTarget = newPosition;
        hasPendingSeek = true;
        fadingOut = true;
    }
    else
    {
        position = newPosition;
    }
}

void Transport::setLooping(bool shouldLoop) noexcept
{
    looping = shouldLoop;
}

void Transport::setLoopRange(juce::int64 start, juce::int64 end) noexcept
{
    loopStart = std::max(juce::int64{0}, start);
    loopEnd = std::max(juce::int64{0}, end);
}

juce::int64 Transport::getLoopStart() const noexcept
{
    return hasLoopRange() ? loopStart : 0;
}

juce::int64 Transport::getLoopEnd() const noexcept
{
    return hasLoopRange() ? loopEnd : length;
}

void Transport::render(TimelineRenderer& renderer, juce::AudioBuffer<float>& output,
                       int startSample, int numSamples) noexcept
{
    jassert(startSample >= 0 && startSample + numSamples <= output.getNumSamples());

    int done = 0;

    while (done < numSamples)
    {
        if (!running)
        {
            renderer.renderStopped(output, startSample + done, numSamples - done);
            return;
        }

        // Arriving exactly at the loop end wraps; a position already past it (after a
        // seek) plays on to the end of the timeline.
        const juce::int64 end = getLoopEnd();
        const juce::int64 limit = looping && position <= end ? end : length;
        if (position >= limit)
        {
            reachEnd();
            continue;
        }

        int segment = static_cast<int>(std::min<juce::int64>(numSamples - done, limit - position));

        const int levelsToGo = std::abs(targetLevel() - level);
        if (levelsToGo > 0)
            segment = std::min(segment, levelsToGo);

        const int step = level < targetLevel() ? 1 : (level > targetLevel() ? -1 : 0);
        jassert(step != 0 || level == fadeLength); // not fading while running: full gain

        renderer.renderTimeline(position, output, startSample + done, segment,
                                TimelineFade{level, step, fadeLength});
        level += step * segment;
        position += segment;
        done += segment;

        if (fadingOut && level == 0)
            finishFadeOut();
    }
}

void Transport::startFadeIn() noexcept
{
    running = true;
    fadingOut = false;
    level = 0;
}

void Transport::finishFadeOut() noexcept
{
    fadingOut = false;
    level = 0;

    if (hasPendingSeek)
    {
        position = seekTarget;
        hasPendingSeek = false;
    }

    if (state == TransportState::playing)
    {
        startFadeIn(); // a seek: carry on from the new position
        return;
    }

    running = false;
    if (state == TransportState::stopped)
        position = returnPosition;
}

void Transport::reachEnd() noexcept
{
    const juce::int64 start = getLoopStart();
    if (looping && position == getLoopEnd() && getLoopEnd() > start)
    {
        position = start;
        return;
    }

    state = TransportState::stopped;
    position = returnPosition;
    running = false;
    fadingOut = false;
    level = 0;
    hasPendingSeek = false;
}

} // namespace daw::engine
