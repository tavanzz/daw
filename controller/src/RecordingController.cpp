#include "daw/controller/RecordingController.h"

#include <algorithm>
#include <cmath>
#include <set>

namespace daw::controller
{

namespace
{

juce::String formatTime(juce::int64 samples, double sampleRate)
{
    const auto milliseconds = static_cast<juce::int64>(
        std::llround(static_cast<double>(std::max(juce::int64{0}, samples)) * 1000.0 / sampleRate));
    return juce::String(milliseconds / 60000) + ":" +
           juce::String((milliseconds / 1000) % 60).paddedLeft('0', 2) + "." +
           juce::String(milliseconds % 1000).paddedLeft('0', 3);
}

// "Vocals 1.wav", "Vocals 2.wav", ...: the first name no file has, nor any taken
// by this recording.
juce::File nameForTake(const juce::File& folder, const juce::String& trackName,
                       std::set<juce::String>& taken)
{
    auto base = juce::File::createLegalFileName(trackName.trim());
    if (base.isEmpty())
        base = "Track";

    for (int number = 1;; ++number)
    {
        const auto file = folder.getChildFile(base + " " + juce::String(number) + ".wav");
        if (!file.exists() && taken.insert(file.getFullPathName()).second)
            return file;
    }
}

} // namespace

RecordingController::RecordingController(engine::Engine& engineToUse)
    : engine(engineToUse)
{
}

RecordingController::~RecordingController()
{
    if (writer == nullptr)
        return;

    // The engine must let go of the setup before it is freed: stop, and give it the
    // last latency's worth of blocks.
    engine.stop();
    const auto deadline = juce::Time::getMillisecondCounter() + 2000;
    while (engine.getRecordingStatus().state != engine::RecordingState::complete &&
           juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(1);

    if (engine.getRecordingStatus().state != engine::RecordingState::complete)
        (void) setup.release(); // the audio thread may still hold it: never free it

    writer->finish();
}

juce::Result RecordingController::start(const RecordingRequest& newRequest)
{
    if (isActive())
        return juce::Result::fail("A recording is already running.");
    if (newRequest.tracks.empty())
        return juce::Result::fail("Arm a track with an input to record.");
    if (!newRequest.folder.createDirectory())
        return juce::Result::fail("The folder \"" + newRequest.folder.getFullPathName() +
                                  "\" could not be created.");

    int numChannels = 0;
    for (const auto& track : newRequest.tracks)
        numChannels += track.numInputChannels;

    const auto bytesPerSecond = engine::io::RecordingWriter::bytesPerSecond(
        newRequest.sampleRate, newRequest.bitsPerSample, numChannels);
    const auto secondsFree = static_cast<double>(bytesFree(newRequest.folder)) / bytesPerSecond;
    if (secondsFree < minimumSecondsToStart)
        return juce::Result::fail("There is not enough disk space to record: about " +
                                  juce::String(static_cast<int>(secondsFree)) +
                                  " seconds at this rate. Free some space and try again.");

    engine::io::RecordingWriterSettings settings;
    settings.sampleRate = newRequest.sampleRate;
    settings.bitsPerSample = newRequest.bitsPerSample;
    settings.freeSpace = [this](const juce::File& file) {
        return bytesFree(file.getParentDirectory());
    };
    if (adjustWriterSettings)
        adjustWriterSettings(settings);

    auto newWriter = std::make_unique<engine::io::RecordingWriter>(settings);
    auto newSetup = std::make_unique<engine::RecordingSetup>();
    std::set<juce::String> taken;

    for (const auto& track : newRequest.tracks)
    {
        const auto file = nameForTake(newRequest.folder, track.name, taken);
        auto& fifo = newWriter->addFile(file, track.numInputChannels);
        newSetup->targets.push_back(
            {track.trackId, track.inputChannel, track.numInputChannels, &fifo});
    }

    if (const auto started = newWriter->start(); started.failed())
        return started;

    newSetup->startPosition = newRequest.startPosition;
    newSetup->countInLength = std::max(juce::int64{0}, newRequest.countInLength);
    newSetup->latency = std::max(juce::int64{0}, newRequest.latency);
    newSetup->punchIn = newRequest.punchIn;
    newSetup->punchOut = newRequest.punchOut;

    request = newRequest;
    writer = std::move(newWriter);
    setup = std::move(newSetup);
    stopRequested = false;
    finishRequested = false;
    discarding = false;
    problem.clear();

    engine.startRecording(*setup);
    return juce::Result::ok();
}

void RecordingController::stop()
{
    if (!isActive() || stopRequested)
        return;

    stopRequested = true;
    engine.stop();
}

void RecordingController::discard()
{
    if (!isActive())
        return;

    discarding = true;
    stop();
}

std::optional<RecordingOutcome> RecordingController::update()
{
    if (!isActive())
        return std::nullopt;

    const auto status = engine.getRecordingStatus();
    const bool capturing = status.state == engine::RecordingState::starting ||
                           status.state == engine::RecordingState::recording;

    // Problems end the recording; what was captured is kept.
    if (capturing)
    {
        const auto writerProblem = writer->getProblem();
        if (writerProblem == engine::io::RecordingWriter::Problem::lowDiskSpace)
            endWithProblem("The disk is almost full: recording stopped at " +
                           formatTime(engine.getPlayheadPosition(), request.sampleRate) + ".");
        else if (writerProblem == engine::io::RecordingWriter::Problem::writeFailed)
            endWithProblem("The recording could not be written to disk (is it full, or was it "
                           "disconnected?). What was written up to there is kept.");
    }

    if (status.state != engine::RecordingState::complete)
        return std::nullopt;

    const auto endOfCapture = status.firstPosition + status.samplesCaptured;
    if (status.overrun)
        endWithProblem("The disk could not keep up: recording stopped at " +
                       formatTime(endOfCapture, request.sampleRate) +
                       ". Everything up to there is kept.");
    else if (status.interrupted)
        endWithProblem("The audio device stopped: recording ended at " +
                       formatTime(endOfCapture, request.sampleRate) + ".");

    // Everything captured is in the FIFOs: the writer can complete the files.
    if (discarding)
    {
        writer->discard();
        RecordingOutcome outcome;
        reset();
        return outcome;
    }

    if (!finishRequested)
    {
        writer->finish();
        finishRequested = true;
    }

    if (!writer->isFinished())
        return std::nullopt;

    RecordingOutcome outcome;
    outcome.problem = problem;

    const bool punches = request.punchOut > request.punchIn;
    const auto stopPosition =
        status.overrun || status.interrupted ? endOfCapture : status.stopPosition;
    outcome.keepStart = std::max(punches ? request.punchIn : request.startPosition,
                                 status.firstPosition);
    outcome.keepEnd = std::min(punches ? std::min(request.punchOut, stopPosition) : stopPosition,
                               endOfCapture);

    for (int index = 0; index < writer->getNumFiles(); ++index)
    {
        RecordedTake take;
        take.trackId = setup->targets[static_cast<std::size_t>(index)].trackId;
        take.file = writer->getFile(index);
        take.firstPosition = status.firstPosition;
        take.length = writer->getSamplesWritten(index);
        outcome.takes.push_back(take);
    }

    reset();
    return outcome;
}

RecordingController::State RecordingController::getState() const
{
    if (!isActive())
        return State::idle;

    switch (engine.getRecordingStatus().state)
    {
    case engine::RecordingState::idle:
    case engine::RecordingState::starting:
        return State::countingIn;
    case engine::RecordingState::recording:
        return engine.isCountingIn() ? State::countingIn : State::recording;
    case engine::RecordingState::finishing:
    case engine::RecordingState::complete:
        return State::finishing;
    }

    return State::finishing;
}

double RecordingController::getSecondsLeft() const
{
    return writer != nullptr ? writer->getSecondsLeft() : -1.0;
}

std::vector<RecordingController::LiveTake> RecordingController::getLiveTakes() const
{
    std::vector<LiveTake> takes;
    if (!isActive())
        return takes;

    const bool punches = request.punchOut > request.punchIn;
    for (int index = 0; index < writer->getNumFiles(); ++index)
        takes.push_back({setup->targets[static_cast<std::size_t>(index)].trackId,
                         writer->getFile(index), setup->startPosition - setup->countInLength,
                         punches ? request.punchIn : request.startPosition,
                         writer->getSamplesWritten(index), writer->getPeaks(index)});
    return takes;
}

juce::int64 RecordingController::bytesFree(const juce::File& folder) const
{
    return freeSpace ? freeSpace(folder) : folder.getBytesFreeOnVolume();
}

void RecordingController::endWithProblem(const juce::String& message)
{
    if (problem.isEmpty())
        problem = message;

    // The transport may still be playing (after an overrun it is): stop it, once (a
    // second stop would send it to the start).
    stop();
}

void RecordingController::reset()
{
    writer.reset();
    setup.reset();
    stopRequested = false;
    finishRequested = false;
    discarding = false;
}

} // namespace daw::controller
