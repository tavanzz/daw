#pragma once

#include "daw/engine/Engine.h"
#include "daw/engine/dsp/LatencyProbe.h"

#include <juce_core/juce_core.h>

#include <memory>
#include <optional>

namespace daw::controller
{

/**
    Measures the interface's real round-trip latency with a cable from an output to
    an input, and works out the manual offset that makes recordings line up: the
    measured latency minus what the device reports.

    The engine plays the probe signal (dsp::LatencyProbe) and captures the input;
    the analysis runs here, on the message thread, when the engine is done (a few
    seconds). Message thread only; update() must be called regularly.
*/
class LatencyMeasurement
{
public:
    struct Result
    {
        bool succeeded = false;
        juce::String message;      ///< for the user: the result or what went wrong
        juce::int64 measured = 0;  ///< round trip, samples
        juce::int64 reported = 0;  ///< what the device reports, samples
        juce::int64 offset = 0;    ///< measured - reported: the manual offset to use
        bool inverted = false;     ///< the signal came back with its polarity inverted
    };

    explicit LatencyMeasurement(engine::Engine& engineToUse);

    /** Waits briefly for the engine to finish a measurement in progress. */
    ~LatencyMeasurement();

    LatencyMeasurement(const LatencyMeasurement&) = delete;
    LatencyMeasurement& operator=(const LatencyMeasurement&) = delete;

    /** Starts measuring: plays the probe on outputChannel (0 or 1) and listens on the
        device input inputChannel. reportedLatency is what the device reports for its
        input plus output (the offset is relative to it). */
    juce::Result start(int outputChannel, int inputChannel, juce::int64 reportedLatency);

    [[nodiscard]] bool isRunning() const noexcept { return test != nullptr; }
    [[nodiscard]] float getProgress() const noexcept;

    /** Returns the result once, when the measurement is over. */
    std::optional<Result> update();

private:
    engine::Engine& engine;
    std::unique_ptr<engine::dsp::LatencyProbe> probe;
    std::unique_ptr<engine::LatencyTest> test;
    juce::int64 reported = 0;
    double sampleRate = 0.0;
};

} // namespace daw::controller
