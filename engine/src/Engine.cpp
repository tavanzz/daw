#include "daw/engine/Engine.h"

namespace daw::engine
{

Engine::~Engine()
{
    collectGarbage();
    delete pendingAudio.exchange(nullptr);
    delete waitingToRetire;
    delete current;
}

void Engine::prepare(double newSampleRate, int newMaximumBlockSize)
{
    jassert(newSampleRate > 0.0);
    jassert(newMaximumBlockSize > 0);

    sampleRate = newSampleRate;
    maximumBlockSize = newMaximumBlockSize;
    transport.prepare(newSampleRate);
}

void Engine::process(juce::AudioBuffer<float>& output) noexcept
{
    if (waitingToRetire != nullptr && retired.push(waitingToRetire))
        waitingToRetire = nullptr;

    // New audio waits until the current audio has faded out. Commands wait with it,
    // so that a play() sent right after setAudio() applies to the new audio.
    if (pendingAudio.load(std::memory_order_acquire) != nullptr)
    {
        if (transport.isRunning())
            transport.stop();
        else
            adoptPendingAudio();
    }

    if (pendingAudio.load(std::memory_order_acquire) == nullptr)
        while (const auto command = commands.pop())
            apply(*command);

    if (current != nullptr && juce::exactlyEqual(current->sampleRate, sampleRate))
        transport.render(current->samples, output, 0, output.getNumSamples());
    else
        output.clear();

    outputMeter.process(output, 0, output.getNumSamples());
    publish();
}

void Engine::setAudio(std::unique_ptr<AudioData> audio)
{
    jassert(audio != nullptr);
    collectGarbage();

    // A previous setAudio() the audio thread has not picked up yet never will be:
    // the audio thread only takes ownership by exchanging the pointer out.
    const std::unique_ptr<AudioData> superseded(
        pendingAudio.exchange(audio.release(), std::memory_order_acq_rel));
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

void Engine::collectGarbage()
{
    while (const auto audio = retired.pop())
        delete *audio;
}

TransportState Engine::getTransportState() const noexcept
{
    return publishedState.load(std::memory_order_relaxed);
}

juce::int64 Engine::getPlayheadPosition() const noexcept
{
    return publishedPosition.load(std::memory_order_relaxed);
}

juce::int64 Engine::getAudioLength() const noexcept
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
    }
}

void Engine::adoptPendingAudio() noexcept
{
    AudioData* incoming = pendingAudio.exchange(nullptr, std::memory_order_acq_rel);
    if (incoming == nullptr)
        return;

    if (current != nullptr)
        retire(current);

    current = incoming;
    transport.setSourceLength(current->samples.getNumSamples());
}

void Engine::retire(AudioData* audio) noexcept
{
    if (retired.push(audio))
        return;

    // The message thread has not collected for 32 swaps. Keep one back and retry on
    // the next block; beyond that, leaking is the only real-time safe option.
    jassert(waitingToRetire == nullptr);
    if (waitingToRetire == nullptr)
        waitingToRetire = audio;
}

void Engine::publish() noexcept
{
    publishedState.store(transport.getState(), std::memory_order_relaxed);
    publishedPosition.store(transport.getPosition(), std::memory_order_relaxed);
    publishedLength.store(current != nullptr ? current->samples.getNumSamples() : 0,
                          std::memory_order_relaxed);
    publishedLooping.store(transport.isLooping(), std::memory_order_relaxed);
}

} // namespace daw::engine
