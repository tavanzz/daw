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

void Transport::setSourceLength(juce::int64 lengthInSamples) noexcept
{
    jassert(lengthInSamples >= 0);

    sourceLength = std::max(juce::int64{0}, lengthInSamples);
    state = TransportState::stopped;
    position = 0;
    loopStart = 0;
    loopEnd = sourceLength;
    running = false;
    fadingOut = false;
    level = 0;
    hasPendingSeek = false;
}

void Transport::play() noexcept
{
    if (sourceLength == 0 || state == TransportState::playing)
        return;

    const bool wasStopped = state == TransportState::stopped;
    state = TransportState::playing;

    if (!running)
    {
        startFadeIn();
    }
    else if (wasStopped)
    {
        // Still fading out after stop(): finish stopping, then start again from 0.
        seekTarget = 0;
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
    const bool wasPlaying = state == TransportState::playing;
    state = TransportState::stopped;
    hasPendingSeek = false;

    if (wasPlaying)
        fadingOut = true; // the position returns to 0 when the fade-out ends
    else if (!running)
        position = 0;
}

void Transport::seek(juce::int64 newPosition) noexcept
{
    newPosition = std::clamp(newPosition, juce::int64{0}, sourceLength);

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
    start = std::clamp(start, juce::int64{0}, sourceLength);
    end = std::clamp(end, juce::int64{0}, sourceLength);

    if (end > start)
    {
        loopStart = start;
        loopEnd = end;
    }
    else
    {
        loopStart = 0;
        loopEnd = sourceLength;
    }
}

void Transport::render(const juce::AudioBuffer<float>& source, juce::AudioBuffer<float>& output,
                       int startSample, int numSamples) noexcept
{
    jassert(source.getNumSamples() == sourceLength);
    jassert(startSample >= 0 && startSample + numSamples <= output.getNumSamples());

    int done = 0;

    while (done < numSamples)
    {
        if (!running)
        {
            for (int channel = 0; channel < output.getNumChannels(); ++channel)
                output.clear(channel, startSample + done, numSamples - done);
            return;
        }

        // Arriving exactly at the loop end wraps; a position already past it (after a
        // seek) plays on to the end of the source.
        const juce::int64 limit = looping && position <= loopEnd ? loopEnd : sourceLength;
        if (position >= limit)
        {
            reachEnd();
            continue;
        }

        int length = static_cast<int>(std::min<juce::int64>(numSamples - done, limit - position));

        const int levelsToGo = std::abs(targetLevel() - level);
        if (levelsToGo > 0)
            length = std::min(length, levelsToGo);

        writeSegment(source, output, startSample + done, length);
        position += length;
        done += length;

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
        position = 0;
}

void Transport::reachEnd() noexcept
{
    if (looping && position == loopEnd && loopEnd > loopStart)
    {
        position = loopStart;
        return;
    }

    state = TransportState::stopped;
    position = 0;
    running = false;
    fadingOut = false;
    level = 0;
    hasPendingSeek = false;
}

void Transport::writeSegment(const juce::AudioBuffer<float>& source,
                             juce::AudioBuffer<float>& output, int outputStart,
                             int length) noexcept
{
    const int step = level < targetLevel() ? 1 : (level > targetLevel() ? -1 : 0);
    const auto fullLevel = static_cast<float>(fadeLength);
    const int sourceChannels = source.getNumChannels();
    const auto sourceStart = static_cast<int>(position);

    for (int channel = 0; channel < output.getNumChannels(); ++channel)
    {
        float* destination = output.getWritePointer(channel, outputStart);

        if (channel >= 2 || sourceChannels == 0)
        {
            juce::FloatVectorOperations::clear(destination, length);
            continue;
        }

        const float* samples = source.getReadPointer(std::min(channel, sourceChannels - 1), sourceStart);

        if (step == 0)
        {
            jassert(level == fadeLength); // not fading while running means full gain
            juce::FloatVectorOperations::copy(destination, samples, length);
            continue;
        }

        int sampleLevel = level;
        for (int i = 0; i < length; ++i)
        {
            sampleLevel += step;
            // Division, not a reciprocal multiply: the last fade-in sample gets exactly 1.
            destination[i] = samples[i] * (static_cast<float>(sampleLevel) / fullLevel);
        }
    }

    level += step * length;
}

} // namespace daw::engine
