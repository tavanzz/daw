#include "daw/engine/Engine.h"

#include <algorithm>
#include <cmath>

namespace daw::engine
{

Engine::~Engine()
{
    collectGarbage();
    delete held;
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
    metronome.prepare(newSampleRate);
    countInRemaining = 0;

    // The device stopped: a recording or a latency test cannot go on.
    recorder.interrupt();
    if (latencyTest != nullptr)
    {
        latencyTest = nullptr;
        latencyTestDone.store(true, std::memory_order_release);
    }
    transport.setLinear(false);
    recordingTransport = false;
    silentArrangement.sampleRate = newSampleRate;
    stereoBuffer.setSize(2, newMaximumBlockSize);
    crossfadeLength =
        std::max(1, static_cast<int>(std::lround(crossfadeSeconds * newSampleRate)));
    readyAheadLength = std::max(juce::int64{1}, std::llround(readyAheadSeconds * newSampleRate));
    maximumWait = std::llround(maximumWaitSeconds * newSampleRate);
    waitingForData = false;

    // The device was stopped: whatever was crossfading is over.
    finishCrossfade();
    latencySwitch = LatencySwitch::none;
    switchLevel = crossfadeLength;
    history.reset(streamPosition, transport.getPosition());
}

void Engine::process(const AudioInputs& inputs, juce::AudioBuffer<float>& output) noexcept
{
    // Decaying tails would otherwise turn into denormals, which some processors
    // compute very slowly. Exports run in the same mode, so they stay identical.
    const juce::ScopedNoDenormals noDenormals;
    renderClock.begin();
    processBlock(inputs, output);
    renderClock.end();
}

void Engine::processBlock(const AudioInputs& inputs, juce::AudioBuffer<float>& output) noexcept
{
    inputMeters.process(inputs, output.getNumSamples());

    if (maximumBlockSize <= 0)
    {
        output.clear();
        return;
    }

    if (waitingToRetire != nullptr && retired.push(waitingToRetire))
        waitingToRetire = nullptr;

    takeNewSnapshot();

    while (const auto command = commands.pop())
        apply(*command);

    startPendingRecording();

    if (runLatencyTest(inputs, output))
    {
        streamPosition += output.getNumSamples();
        publish();
        return;
    }

    startWhenReady();

    // A recording's transport stays linear until its stop has faded out.
    if (recordingTransport && !recorder.isRecording() && !transport.isRunning() &&
        countInRemaining == 0)
    {
        transport.setLinear(false);
        recordingTransport = false;
    }

    recorder.capture(inputs, streamPosition, output.getNumSamples());

    blockInputs = &inputs;
    meteredChannels = std::min(output.getNumChannels(), 2);

    if (output.getNumChannels() >= 2)
    {
        renderOffset = 0;
        renderBlock(output, output.getNumSamples());
    }
    else
    {
        // A mono (or output-less) device gets the left channel.
        for (int done = 0; done < output.getNumSamples(); done += maximumBlockSize)
        {
            const int chunk = std::min(maximumBlockSize, output.getNumSamples() - done);
            renderOffset = done;
            renderBlock(stereoBuffer, chunk);
            if (output.getNumChannels() == 1)
                output.copyFrom(0, done, stereoBuffer, 0, 0, chunk);
        }
    }

    blockInputs = nullptr;
    applyLatencySwitch(output);
    streamPosition += output.getNumSamples();
    publish();
}

void Engine::startPendingRecording() noexcept
{
    // A recording starts from a silent transport, so that its first sample is where
    // the timeline really starts...
    if (transport.isRunning() || countInRemaining > 0 || pendingRecording == nullptr)
    {
        recordingWaitSince = streamPosition;
        return;
    }

    // ...and once what it plays at the start is loaded (or it has waited enough).
    if (pendingRecording->countInLength <= 0 && !isReadyAt(pendingRecording->startPosition) &&
        streamPosition - recordingWaitSince < maximumWait)
        return;

    const auto* setup = std::exchange(pendingRecording, nullptr);
    waitingForData = false;

    // What is heard lags what is read by the effects' latency: so does the recording.
    recordingLatency = getCurrentLatency();
    recorder.begin(*setup, streamPosition + recordingLatency);
    recordingTransport = true;
    transport.setLinear(true);
    transport.seek(setup->startPosition);

    if (setup->countInLength > 0)
    {
        countInRemaining = setup->countInLength;
        countInPosition = setup->startPosition - setup->countInLength;
    }
    else
    {
        transport.play();
    }
}

bool Engine::runLatencyTest(const AudioInputs& inputs, juce::AudioBuffer<float>& output) noexcept
{
    if (latencyTest == nullptr && !transport.isRunning() && countInRemaining == 0 &&
        !recorder.isRecording() && pendingLatencyTest.load(std::memory_order_relaxed) != nullptr)
    {
        latencyTest = pendingLatencyTest.exchange(nullptr, std::memory_order_acquire);
        latencyTestPosition = 0;
    }

    if (latencyTest == nullptr)
        return false;

    const auto numSamples = static_cast<std::size_t>(output.getNumSamples());
    output.clear();

    const auto& signal = *latencyTest->signal;
    if (latencyTest->outputChannel < output.getNumChannels() && latencyTestPosition < signal.size())
    {
        const auto count = std::min(numSamples, signal.size() - latencyTestPosition);
        juce::FloatVectorOperations::copy(output.getWritePointer(latencyTest->outputChannel),
                                          signal.data() + latencyTestPosition,
                                          static_cast<int>(count));
    }

    auto& captured = latencyTest->captured;
    if (latencyTestPosition < captured.size())
    {
        const auto count = std::min(numSamples, captured.size() - latencyTestPosition);
        if (const float* input = inputs.channel(latencyTest->inputChannel))
            juce::FloatVectorOperations::copy(captured.data() + latencyTestPosition, input,
                                              static_cast<int>(count));
        else
            juce::FloatVectorOperations::clear(captured.data() + latencyTestPosition,
                                               static_cast<int>(count));
    }

    latencyTestPosition += numSamples;
    const auto total = std::max<std::size_t>(1, captured.size());
    latencyTestProgress.store(
        static_cast<float>(std::min(latencyTestPosition, total)) / static_cast<float>(total),
        std::memory_order_relaxed);

    if (latencyTestPosition >= captured.size())
    {
        latencyTest = nullptr;
        latencyTestDone.store(true, std::memory_order_release);
    }

    return true;
}

void Engine::endRecording() noexcept
{
    if (recorder.isRecording())
        recorder.end(streamPosition + recordingLatency);
}

bool Engine::isReadyAt(juce::int64 position) const noexcept
{
    return !canPlay(current) ||
           isArrangementReady(*current->arrangement, position, readyAheadLength);
}

void Engine::startWhenReady() noexcept
{
    if (!waitingForData || transport.isRunning())
        return;

    if (isReadyAt(transport.getPosition()) || streamPosition - waitingSince >= maximumWait)
    {
        waitingForData = false;
        transport.play();
    }
}

bool Engine::autoMonitorsAt(juce::int64 timelinePosition) const noexcept
{
    if (countInRemaining > 0 || transport.getState() != TransportState::playing)
        return true;

    const auto* setup = recorder.isRecording() ? recorder.getSetup() : nullptr;
    if (setup == nullptr)
        return false;

    const bool punches = setup->punchOut > setup->punchIn;
    return !punches || (timelinePosition >= setup->punchIn && timelinePosition < setup->punchOut);
}

int Engine::pieceLength(juce::int64 timelinePosition, int available) const noexcept
{
    auto length = static_cast<juce::int64>(std::min(maximumBlockSize, available));

    // Monitoring switches on the exact sample of a punch in or out.
    if (const auto* setup = recorder.isRecording() ? recorder.getSetup() : nullptr)
        for (const auto boundary : {setup->punchIn, setup->punchOut})
            if (setup->punchOut > setup->punchIn && boundary > timelinePosition)
                length = std::min(length, boundary - timelinePosition);

    return static_cast<int>(length);
}

void Engine::renderBlock(juce::AudioBuffer<float>& output, int numSamples) noexcept
{
    int done = 0;

    if (countInRemaining > 0)
    {
        done = static_cast<int>(std::min<juce::int64>(numSamples, countInRemaining));
        renderCountIn(output, 0, done);
        countInRemaining -= done;
        countInPosition += done;

        // Playback starts on the sample after the count-in, with the usual fade-in.
        if (countInRemaining == 0)
            transport.play();
    }

    if (done < numSamples)
        transport.render(*this, output, done, numSamples - done);
}

void Engine::renderCountIn(juce::AudioBuffer<float>& output, int outputStart,
                           int numSamples) noexcept
{
    const BeatGrid* beats = canPlay(current) ? current->arrangement->beats.get() : nullptr;

    for (int done = 0; done < numSamples; done += maximumBlockSize)
    {
        const int piece = std::min(maximumBlockSize, numSamples - done);
        auto context = makeContext(outputStart + done);
        context.timelineRunning = false;
        context.autoMonitorsInput = true;
        recordRead(outputStart + done, TimelineHistory::Kind::countIn, countInPosition + done,
                   beats != nullptr);
        renderPiece(countInPosition + done, output, outputStart + done, piece, context);
        finishPiece(output, outputStart + done, piece);
    }
}

void Engine::recordRead(int outputStart, TimelineHistory::Kind kind, juce::int64 position,
                        bool clicks) noexcept
{
    history.record({streamPosition + renderOffset + outputStart, position, kind, clicks});
}

void Engine::finishPiece(juce::AudioBuffer<float>& output, int outputStart,
                         int numSamples) noexcept
{
    // The meter shows the mix: it measures before the clicks are added.
    outputMeter.process(output, outputStart, numSamples, meteredChannels);

    if (output.getNumChannels() < 2)
        return;

    const bool playable = canPlay(current);
    const BeatGrid* beats = playable ? current->arrangement->beats.get() : nullptr;
    const float gain = playable ? current->arrangement->metronomeGain : 0.0f;
    float* const left = output.getWritePointer(0, outputStart);
    float* const right = output.getWritePointer(1, outputStart);

    // The clicks go with what is heard now, which was read latency samples ago.
    const juce::int64 heard = streamPosition + renderOffset + outputStart - getCurrentLatency();
    history.forEach(heard, numSamples,
                    [&](int offset, int length, const TimelineHistory::Stretch& stretch) {
                        metronome.render(stretch.clicks ? beats : nullptr, stretch.position, gain,
                                         left + offset, right + offset, length);
                    });
}

int Engine::getCurrentLatency() const noexcept
{
    return canPlay(current) ? current->mix.latency : 0;
}

bool Engine::mayBeHeard() const noexcept
{
    return transport.isRunning() || countInRemaining > 0 ||
           (current != nullptr &&
            (current->mix.isHearingInput() || current->arrangement->hasEffects()));
}

MixContext Engine::makeContext(int outputStart) const noexcept
{
    MixContext context;
    context.inputs = blockInputs;
    context.inputOffset = renderOffset + outputStart;
    context.history = &history;
    context.stream = streamPosition + renderOffset + outputStart;
    context.autoMonitorsInput = autoMonitorsAt(transport.getPosition());
    return context;
}

void Engine::renderTimeline(juce::int64 timelineStart, juce::AudioBuffer<float>& output,
                            int outputStart, int numSamples, const TimelineFade& fade) noexcept
{
    const bool clicks = canPlay(current) && current->arrangement->metronomeEnabled &&
                        transport.getState() == TransportState::playing;

    // Devices may deliver more than they announced: render in pieces the mixer fits.
    for (int done = 0; done < numSamples;)
    {
        const int piece = pieceLength(timelineStart + done, numSamples - done);
        auto context = makeContext(outputStart + done);
        context.fade = fade.from(done);
        context.autoMonitorsInput = autoMonitorsAt(timelineStart + done);
        recordRead(outputStart + done, TimelineHistory::Kind::playing, timelineStart + done, clicks);
        renderPiece(timelineStart + done, output, outputStart + done, piece, context);
        finishPiece(output, outputStart + done, piece);
        done += piece;
    }
}

void Engine::renderStopped(juce::AudioBuffer<float>& output, int outputStart,
                           int numSamples) noexcept
{
    for (int done = 0; done < numSamples; done += maximumBlockSize)
    {
        const int piece = std::min(maximumBlockSize, numSamples - done);
        auto context = makeContext(outputStart + done);
        context.timelineRunning = false;
        recordRead(outputStart + done, TimelineHistory::Kind::stopped, transport.getPosition(),
                   false);
        renderPiece(transport.getPosition(), output, outputStart + done, piece, context);
        finishPiece(output, outputStart + done, piece);
    }
}

void Engine::renderPiece(juce::int64 timelineStart, juce::AudioBuffer<float>& output,
                         int outputStart, int numSamples, const MixContext& context) noexcept
{
    if (!canPlay(current))
    {
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.clear(channel, outputStart, numSamples);
        return;
    }

    if (fadingOut == nullptr)
    {
        mixer.render(*current->arrangement, current->mix, timelineStart, output, outputStart,
                     numSamples, context);
        return;
    }

    // Track by track, the clips that changed crossfade from the old arrangement to
    // the new, and tracks that are gone fade out. (An arrangement at another rate
    // plays nothing, so everything fades in from silence.)
    auto crossfading = context;
    const bool oldPlays = canPlay(fadingOut);
    crossfading.previous = oldPlays ? fadingOut->arrangement.get() : &silentArrangement;
    crossfading.previousState = oldPlays ? &fadingOut->mix : &silentMix;
    crossfading.crossfadeDone = crossfadeDone;
    crossfading.crossfadeLength = crossfadeLength;
    mixer.render(*current->arrangement, current->mix, timelineStart, output, outputStart,
                 numSamples, crossfading);

    crossfadeDone += std::min(numSamples, crossfadeLength - crossfadeDone);
    if (crossfadeDone >= crossfadeLength)
        finishCrossfade();
}

void Engine::setArrangement(std::unique_ptr<Arrangement> arrangement)
{
    jassert(arrangement != nullptr);
    collectGarbage();

    arrangement->updateLayoutHash();
    auto snapshot = std::make_unique<Snapshot>();
    snapshot->mix = MixState::startingAt(
        *arrangement, std::max(MixState::defaultBusBlockSize, maximumBlockSize));
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

void Engine::playWithCountIn(juce::int64 position, juce::int64 countInLength)
{
    send({Command::Type::countIn, position, countInLength});
}

void Engine::startRecording(const RecordingSetup& setup)
{
    // Through the command queue, so that it keeps its place among stop and play.
    recorder.markStarting();
    send({Command::Type::record, 0, 0, &setup});
}

RecordingStatus Engine::getRecordingStatus() const noexcept
{
    return recorder.getStatus();
}

void Engine::startLatencyTest(LatencyTest& test)
{
    jassert(pendingLatencyTest.load() == nullptr);
    latencyTestDone.store(false);
    latencyTestProgress.store(0.0f);
    pendingLatencyTest.store(&test, std::memory_order_release);
}

bool Engine::isLatencyTestDone() const noexcept
{
    return latencyTestDone.load(std::memory_order_acquire);
}

float Engine::getLatencyTestProgress() const noexcept
{
    return latencyTestProgress.load(std::memory_order_relaxed);
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

int Engine::getLatency() const noexcept
{
    return publishedLatency.load(std::memory_order_relaxed);
}

juce::int64 Engine::getLength() const noexcept
{
    return publishedLength.load(std::memory_order_relaxed);
}

bool Engine::isLooping() const noexcept
{
    return publishedLooping.load(std::memory_order_relaxed);
}

bool Engine::isCountingIn() const noexcept
{
    return publishedCountingIn.load(std::memory_order_relaxed);
}

bool Engine::isWaitingForData() const noexcept
{
    return publishedWaiting.load(std::memory_order_relaxed);
}

juce::int64 Engine::getLoopStart() const noexcept
{
    return publishedLoopStart.load(std::memory_order_relaxed);
}

juce::int64 Engine::getLoopEnd() const noexcept
{
    return publishedLoopEnd.load(std::memory_order_relaxed);
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
    // While recording, the transport goes straight on: seeks and other starts are
    // ignored, pause stops.
    if (recorder.isRecording())
    {
        switch (command.type)
        {
        case Command::Type::play:
        case Command::Type::seek:
        case Command::Type::countIn:
        case Command::Type::record: // one recording at a time (the session sees to it)
            return;
        case Command::Type::pause:
        case Command::Type::stop:
        case Command::Type::resetTransport:
        {
            endRecording();
            const bool wasCountingIn = countInRemaining > 0;
            countInRemaining = 0;
            if (command.type == Command::Type::resetTransport)
                transport.reset();
            else if (!wasCountingIn) // during a count-in the transport has not started
                transport.stop();
            return;
        }
        case Command::Type::setLooping:
            break;
        }
    }

    // Anything that moves or stops the transport ends a count-in.
    const bool endsCountIn = command.type == Command::Type::pause ||
                             command.type == Command::Type::stop ||
                             command.type == Command::Type::seek ||
                             command.type == Command::Type::resetTransport;
    const bool wasCountingIn = countInRemaining > 0;
    if (endsCountIn)
        countInRemaining = 0;

    // Stopping or pausing gives up waiting for streamed audio, and a recording that
    // has not started yet (it waits for the transport, or for its audio): it never
    // will. A seek waits on.
    if (command.type == Command::Type::pause || command.type == Command::Type::stop ||
        command.type == Command::Type::resetTransport)
    {
        waitingForData = false;
        if (const auto* pending = std::exchange(pendingRecording, nullptr))
            recorder.cancel(*pending);
    }

    // Playback that would start, or jump, onto audio still loading waits for it.
    if (command.type == Command::Type::play && countInRemaining == 0 && !transport.isRunning() &&
        transport.getState() != TransportState::playing && !isReadyAt(transport.getPosition()))
    {
        waitingForData = true;
        waitingSince = streamPosition;
        return;
    }

    if (command.type == Command::Type::seek && transport.getState() == TransportState::playing &&
        !isReadyAt(std::max(juce::int64{0}, command.value)))
    {
        transport.pause(); // fades out, then lands on the target and waits there
        transport.seek(command.value);
        waitingForData = true;
        waitingSince = streamPosition;
        return;
    }

    // Stopping or pausing a count-in leaves the transport where it waited to start.
    switch (command.type)
    {
    case Command::Type::play:
        if (countInRemaining == 0)
            transport.play();
        break;
    case Command::Type::pause:
        if (!wasCountingIn)
            transport.pause();
        break;
    case Command::Type::stop:
        if (!wasCountingIn)
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
    case Command::Type::countIn:
        transport.seek(command.value);
        if (transport.isRunning() || command.length <= 0)
        {
            transport.play();
        }
        else
        {
            countInRemaining = command.length;
            countInPosition = command.value - command.length;
        }
        break;
    case Command::Type::record:
        pendingRecording = command.recording;
        recordingWaitSince = streamPosition;
        break;
    }
}

void Engine::takeNewSnapshot() noexcept
{
    if (Snapshot* incoming = pendingSnapshot.exchange(nullptr, std::memory_order_acq_rel))
    {
        if (held != nullptr)
            retire(held); // superseded before it could play
        held = incoming;
    }

    // One change at a time: a newer one waits for the crossfade.
    if (held == nullptr || fadingOut != nullptr || latencySwitch == LatencySwitch::fadingOut)
        return;

    // A change of latency puts the audio at other times: rather than jump, the output
    // fades out first, stays silent while the new delays fill, then fades in.
    const bool movesAudio = current != nullptr && canPlay(current) && canPlay(held) &&
                            held->mix.movesAudioFrom(current->mix, *current->arrangement,
                                                     *held->arrangement);

    if (movesAudio && mayBeHeard() &&
        (latencySwitch == LatencySwitch::none || latencySwitch == LatencySwitch::fadingIn))
    {
        latencySwitch = LatencySwitch::fadingOut;
        return;
    }

    const bool dipping = latencySwitch == LatencySwitch::silent ||
                         latencySwitch == LatencySwitch::filling;
    adopt(std::exchange(held, nullptr));

    if (dipping)
    {
        latencySwitch = LatencySwitch::filling;
        fillRemaining = current->mix.latency;
    }
}

void Engine::adopt(Snapshot* incoming) noexcept
{
    if (current != nullptr)
    {
        // While anything may be heard (effects ring on after a stop), gains carry on
        // from where they are and ramp to the new targets; while silent, the new
        // gains simply apply.
        const bool heard = mayBeHeard();
        if (heard)
            incoming->mix.continueFrom(current->mix, *current->arrangement,
                                       *incoming->arrangement, mixer.getGainRampLength());

        // Changed clips crossfade while the timeline plays; tracks that are gone fade
        // out whenever they may be heard.
        const bool soundsDifferent =
            incoming->arrangement->layoutHash != current->arrangement->layoutHash;
        const bool losesTracks = !hasEveryTrackOf(*incoming->arrangement, *current->arrangement);

        if (canPlay(current) &&
            ((transport.isRunning() && soundsDifferent) || (heard && losesTracks)))
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

bool Engine::hasEveryTrackOf(const Arrangement& arrangement, const Arrangement& other) noexcept
{
    for (std::size_t i = 0; i < other.tracks.size(); ++i)
    {
        const auto id = other.tracks[i].id;
        if (i < arrangement.tracks.size() && arrangement.tracks[i].id == id)
            continue;
        if (std::none_of(arrangement.tracks.begin(), arrangement.tracks.end(),
                         [id](const ArrangementTrack& track) { return track.id == id; }))
            return false;
    }
    return true;
}

void Engine::applyLatencySwitch(juce::AudioBuffer<float>& output) noexcept
{
    if (latencySwitch == LatencySwitch::none)
        return;

    const auto fullLevel = static_cast<float>(crossfadeLength);
    for (int i = 0; i < output.getNumSamples(); ++i)
    {
        switch (latencySwitch)
        {
        case LatencySwitch::fadingOut:
            if (--switchLevel <= 0)
            {
                switchLevel = 0;
                latencySwitch = LatencySwitch::silent;
            }
            break;
        case LatencySwitch::filling:
            if (fillRemaining > 0)
                --fillRemaining;
            else
                latencySwitch = LatencySwitch::fadingIn;
            break;
        case LatencySwitch::fadingIn:
            if (++switchLevel >= crossfadeLength)
            {
                switchLevel = crossfadeLength;
                latencySwitch = LatencySwitch::none;
            }
            break;
        case LatencySwitch::silent:
        case LatencySwitch::none:
            break;
        }

        const float gain = static_cast<float>(switchLevel) / fullLevel;
        for (int channel = 0; channel < output.getNumChannels(); ++channel)
            output.getWritePointer(channel)[i] *= gain;
    }
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
    publishedState.store(waitingForData ? TransportState::playing : transport.getState(),
                         std::memory_order_relaxed);
    publishedWaiting.store(waitingForData, std::memory_order_relaxed);
    publishedLoopStart.store(transport.getLoopStart(), std::memory_order_relaxed);
    publishedLoopEnd.store(transport.getLoopEnd(), std::memory_order_relaxed);
    // The playhead shows what is heard: with effects that delay the audio, what was
    // read latency samples ago.
    const int latency = getCurrentLatency();
    publishedPosition.store(latency == 0 ? transport.getPosition()
                                         : history.at(streamPosition - latency).position,
                            std::memory_order_relaxed);
    publishedLatency.store(latency, std::memory_order_relaxed);
    publishedLength.store(transport.getLength(), std::memory_order_relaxed);
    publishedLooping.store(transport.isLooping(), std::memory_order_relaxed);
    publishedCountingIn.store(countInRemaining > 0, std::memory_order_relaxed);
}

} // namespace daw::engine
