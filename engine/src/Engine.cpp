#include "daw/engine/Engine.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{

Engine::~Engine()
{
    collectGarbage();
    delete pendingSnapshot.exchange(nullptr);
    delete waitingToRetire;
    delete fadingOut;
    delete current;
}

void Engine::prepare(double newSampleRate, int newMaximumBlockSize)
{
    jassert(newSampleRate > 0.0);
    jassert(newMaximumBlockSize > 0);

    sampleRate = newSampleRate;
    maximumBlockSize = newMaximumBlockSize;
    transport.prepare(newSampleRate);
    mixer.prepare(newSampleRate, newMaximumBlockSize);
    crossfadeBuffer.setSize(2, newMaximumBlockSize);
    stereoBuffer.setSize(2, newMaximumBlockSize);
    crossfadeLength =
        std::max(1, static_cast<int>(std::lround(crossfadeSeconds * newSampleRate)));

    // The device was stopped: whatever was crossfading is over.
    finishCrossfade();
}

void Engine::process(const AudioInputs& inputs, juce::AudioBuffer<float>& output) noexcept
{
    inputMeters.process(inputs, output.getNumSamples());

    if (maximumBlockSize <= 0)
    {
        output.clear();
        return;
    }

    if (waitingToRetire != nullptr && retired.push(waitingToRetire))
        waitingToRetire = nullptr;

    // A crossfade only lasts while the timeline is being rendered.
    if (fadingOut != nullptr && !transport.isRunning())
        finishCrossfade();

    // One change of arrangement at a time: a newer one waits for the crossfade.
    if (fadingOut == nullptr)
        adoptPendingSnapshot();

    while (const auto command = commands.pop())
        apply(*command);

    if (output.getNumChannels() >= 2)
    {
        transport.render(*this, output, 0, output.getNumSamples());
    }
    else
    {
        // A mono (or output-less) device gets the left channel.
        for (int done = 0; done < output.getNumSamples(); done += maximumBlockSize)
        {
            const int chunk = std::min(maximumBlockSize, output.getNumSamples() - done);
            transport.render(*this, stereoBuffer, 0, chunk);
            if (output.getNumChannels() == 1)
                output.copyFrom(0, done, stereoBuffer, 0, 0, chunk);
        }
    }

    outputMeter.process(output, 0, output.getNumSamples());
    publish();
}

void Engine::renderTimeline(juce::int64 timelineStart, juce::AudioBuffer<float>& output,
                            int outputStart, int numSamples) noexcept
{
    // Devices may deliver more than they announced: render in pieces the mixer fits.
    for (int done = 0; done < numSamples; done += maximumBlockSize)
        renderPiece(timelineStart + done, output, outputStart + done,
                    std::min(maximumBlockSize, numSamples - done));
}

void Engine::renderPiece(juce::int64 timelineStart, juce::AudioBuffer<float>& output,
                         int outputStart, int numSamples) noexcept
{
    if (!canPlay(current))
    {
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.clear(channel, outputStart, numSamples);
        return;
    }

    mixer.render(*current->arrangement, current->mix, timelineStart, output, outputStart,
                 numSamples);

    if (fadingOut == nullptr)
        return;

    const int fading = std::min(numSamples, crossfadeLength - crossfadeDone);

    if (canPlay(fadingOut))
        mixer.render(*fadingOut->arrangement, fadingOut->mix, timelineStart, crossfadeBuffer, 0,
                     fading);
    else
        crossfadeBuffer.clear();

    const auto fullLength = static_cast<float>(crossfadeLength);
    for (int channel = 0; channel < 2; ++channel)
    {
        float* mixed = output.getWritePointer(channel, outputStart);
        const float* old = crossfadeBuffer.getReadPointer(channel);

        for (int i = 0; i < fading; ++i)
        {
            const float gain = static_cast<float>(crossfadeDone + i + 1) / fullLength;
            mixed[i] = old[i] + (mixed[i] - old[i]) * gain;
        }
    }

    crossfadeDone += fading;
    if (crossfadeDone >= crossfadeLength)
        finishCrossfade();
}

void Engine::setArrangement(std::unique_ptr<Arrangement> arrangement)
{
    jassert(arrangement != nullptr);
    collectGarbage();

    arrangement->updateLayoutHash();
    auto snapshot = std::make_unique<Snapshot>();
    snapshot->mix = MixState::startingAt(*arrangement);
    snapshot->arrangement = std::move(arrangement);

    // A previous snapshot the audio thread has not picked up yet never will be: the
    // audio thread only takes ownership by exchanging the pointer out.
    const std::unique_ptr<Snapshot> superseded(
        pendingSnapshot.exchange(snapshot.release(), std::memory_order_acq_rel));
}

void Engine::play()
{
    send({Command::Type::play, 0});
}

void Engine::pause()
{
    send({Command::Type::pause, 0});
}

void Engine::stop()
{
    send({Command::Type::stop, 0});
}

void Engine::seek(juce::int64 position)
{
    send({Command::Type::seek, position});
}

void Engine::setLooping(bool shouldLoop)
{
    send({Command::Type::setLooping, shouldLoop ? 1 : 0});
}

void Engine::resetTransport()
{
    send({Command::Type::resetTransport, 0});
}

void Engine::collectGarbage()
{
    while (const auto snapshot = retired.pop())
        delete *snapshot;
}

TransportState Engine::getTransportState() const noexcept
{
    return publishedState.load(std::memory_order_relaxed);
}

juce::int64 Engine::getPlayheadPosition() const noexcept
{
    return publishedPosition.load(std::memory_order_relaxed);
}

juce::int64 Engine::getLength() const noexcept
{
    return publishedLength.load(std::memory_order_relaxed);
}

bool Engine::isLooping() const noexcept
{
    return publishedLooping.load(std::memory_order_relaxed);
}

float Engine::takeOutputPeak(int channel) noexcept
{
    return outputMeter.takePeak(channel);
}

float Engine::takeInputPeak(int channel) noexcept
{
    return inputMeters.takePeak(channel);
}

void Engine::send(Command command)
{
    // 256 commands between two audio blocks would take a runaway caller; dropping
    // one is better than blocking the message thread.
    const bool queued = commands.push(command);
    jassert(queued);
    juce::ignoreUnused(queued);
}

void Engine::apply(const Command& command) noexcept
{
    switch (command.type)
    {
    case Command::Type::play:
        transport.play();
        break;
    case Command::Type::pause:
        transport.pause();
        break;
    case Command::Type::stop:
        transport.stop();
        break;
    case Command::Type::seek:
        transport.seek(command.value);
        break;
    case Command::Type::setLooping:
        transport.setLooping(command.value != 0);
        break;
    case Command::Type::resetTransport:
        transport.reset();
        break;
    }
}

void Engine::adoptPendingSnapshot() noexcept
{
    Snapshot* incoming = pendingSnapshot.exchange(nullptr, std::memory_order_acq_rel);
    if (incoming == nullptr)
        return;

    if (current != nullptr)
    {
        // While playing, gains carry on from where they are and ramp to the new
        // targets; while silent, the new gains simply apply.
        if (transport.isRunning())
            incoming->mix.continueFrom(current->mix, *current->arrangement,
                                       *incoming->arrangement, mixer.getGainRampLength());

        const bool soundsDifferent =
            incoming->arrangement->layoutHash != current->arrangement->layoutHash;

        if (transport.isRunning() && soundsDifferent && canPlay(current))
        {
            fadingOut = current;
            crossfadeDone = 0;
        }
        else
        {
            retire(current);
        }
    }

    current = incoming;
    const auto& arrangement = *current->arrangement;
    transport.setLength(arrangement.length);
    transport.setLoopRange(arrangement.loopStart, arrangement.loopEnd);
}

void Engine::finishCrossfade() noexcept
{
    if (fadingOut != nullptr)
        retire(fadingOut);

    fadingOut = nullptr;
    crossfadeDone = 0;
}

void Engine::retire(Snapshot* snapshot) noexcept
{
    if (retired.push(snapshot))
        return;

    // The message thread has not collected for 256 swaps. Keep one back and retry on
    // the next block; beyond that, leaking is the only real-time safe option.
    jassert(waitingToRetire == nullptr);
    if (waitingToRetire == nullptr)
        waitingToRetire = snapshot;
}

bool Engine::canPlay(const Snapshot* snapshot) const noexcept
{
    return snapshot != nullptr && juce::exactlyEqual(snapshot->arrangement->sampleRate, sampleRate);
}

void Engine::publish() noexcept
{
    publishedState.store(transport.getState(), std::memory_order_relaxed);
    publishedPosition.store(transport.getPosition(), std::memory_order_relaxed);
    publishedLength.store(transport.getLength(), std::memory_order_relaxed);
    publishedLooping.store(transport.isLooping(), std::memory_order_relaxed);
}

} // namespace daw::engine
