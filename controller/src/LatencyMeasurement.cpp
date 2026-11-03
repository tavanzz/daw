#include "daw/controller/LatencyMeasurement.h"

#include <cmath>

namespace daw::controller
{

namespace
{

juce::String milliseconds(juce::int64 samples, double sampleRate)
{
    return juce::String(static_cast<double>(samples) * 1000.0 / sampleRate, 2) + " ms";
}

} // namespace

LatencyMeasurement::LatencyMeasurement(engine::Engine& engineToUse)
    : engine(engineToUse)
{
}

LatencyMeasurement::~LatencyMeasurement()
{
    if (test == nullptr)
        return;

    const auto deadline = juce::Time::getMillisecondCounter() + 10000;
    while (!engine.isLatencyTestDone() && juce::Time::getMillisecondCounter() < deadline)
        juce::Thread::sleep(1);

    if (!engine.isLatencyTestDone())
    {
        // The audio thread may still hold them: never free them.
        (void) test.release();
        (void) probe.release();
    }
}

juce::Result LatencyMeasurement::start(int outputChannel, int inputChannel,
                                       juce::int64 reportedLatency)
{
    if (isRunning())
        return juce::Result::fail("A measurement is already running.");
    if (!(engine.getSampleRate() > 0.0))
        return juce::Result::fail("No audio device is running.");
    if (outputChannel < 0 || outputChannel > 1 || inputChannel < 0 ||
        inputChannel >= engine::AudioInputs::maxChannels)
        return juce::Result::fail("Choose an output and an input.");

    sampleRate = engine.getSampleRate();
    reported = reportedLatency;
    probe = std::make_unique<engine::dsp::LatencyProbe>(sampleRate);
    test = std::make_unique<engine::LatencyTest>();
    test->outputChannel = outputChannel;
    test->inputChannel = inputChannel;
    test->signal = &probe->getSignal();
    test->captured.assign(probe->getSignal().size(), 0.0f);

    engine.stop();
    engine.startLatencyTest(*test);
    return juce::Result::ok();
}

float LatencyMeasurement::getProgress() const noexcept
{
    return isRunning() ? engine.getLatencyTestProgress() : 0.0f;
}

std::optional<LatencyMeasurement::Result> LatencyMeasurement::update()
{
    if (!isRunning() || !engine.isLatencyTestDone())
        return std::nullopt;

    Result result;
    result.reported = reported;

    if (engine.getLatencyTestProgress() < 1.0f)
    {
        result.message = "The audio device stopped during the measurement. Try again.";
    }
    else
    {
        const auto measurement = probe->analyse(test->captured.data(),
                                                static_cast<int>(test->captured.size()));
        if (!measurement.found)
        {
            result.message = measurement.problem;
        }
        else
        {
            result.succeeded = true;
            result.measured = measurement.latency;
            result.offset = measurement.latency - reported;
            result.inverted = measurement.inverted;
            result.message = "Round trip: " + juce::String(measurement.latency) + " samples (" +
                             milliseconds(measurement.latency, sampleRate) +
                             "); the device reports " + juce::String(reported) +
                             ". Offset: " + juce::String(result.offset) + " samples.";
            if (measurement.inverted)
                result.message << " The signal came back with its polarity inverted.";
        }
    }

    test.reset();
    probe.reset();
    return result;
}

} // namespace daw::controller
